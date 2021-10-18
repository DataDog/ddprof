#include "perf_mainloop.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ddprof_worker.h"
#include "ddres.h"
#include "logger.h"
#include "perf.h"
#include "pevent.h"
#include "unwind.h"

#define rmb() __asm__ volatile("lfence" ::: "memory")

#ifdef DDPROF_NATIVE_LIB
#  define WORKER_SHUTDOWN() return;
#else
#  define WORKER_SHUTDOWN() exit(0)
#endif

#define DDRES_CHECK_OR_SHUTDOWN(res, shut_down_process)                        \
  DDRes eval_res = res;                                                        \
  if (IsDDResNotOK(eval_res)) {                                                \
    LG_WRN("[PERF] Shut down worker (error:%s).",                              \
           ddres_error_message(eval_res._what));                               \
    shut_down_process;                                                         \
    WORKER_SHUTDOWN();                                                         \
  }

#define DDRES_GRACEFUL_SHUTDOWN(shut_down_process)                             \
  do {                                                                         \
    LG_NFO("Shutting down worker gracefully");                                 \
    shut_down_process;                                                         \
    WORKER_SHUTDOWN();                                                         \
  } while (0)

DDRes spawn_workers(volatile bool *continue_profiling) {
  pid_t child_pid;

  while ((child_pid = fork())) {
    LG_WRN("Created child %d", child_pid);
    waitpid(child_pid, NULL, 0);

    // Harvest the exit state of the child process.  We will always reset it
    // to false so that a child who segfaults or exits erroneously does not
    // cause a pointless loop of spawning
    if (!*continue_profiling) {
      DDRES_RETURN_WARN_LOG(DD_WHAT_MAINLOOP, "Stop profiling");
    } else {
      *continue_profiling = false;
    }
    LG_NFO("Refreshing worker process");
  }

  return ddres_init();
}

static void pollfd_setup(const PEventHdr *pevent_hdr, struct pollfd *pfd,
                         int *pfd_len) {
  *pfd_len = pevent_hdr->size;
  const PEvent *pes = pevent_hdr->pes;
  // Setup poll() to watch perf_event file descriptors
  for (int i = 0; i < *pfd_len; i++) {
    // NOTE: if fd is negative, it will be ignored
    pfd[i].fd = pes[i].fd;
    pfd[i].events = POLLIN | POLLERR | POLLHUP;
  }
}

static void worker(DDProfContext *ctx, const WorkerAttr *attr,
                   volatile bool *continue_profiling) {
  // Until a restartable terminal condition is met, the worker will set its
  // disposition so that profiling is halted upon its termination
  *continue_profiling = false;

  // Setup poll() to watch perf_event file descriptors
  struct pollfd pfd[MAX_NB_WATCHERS];
  int pfd_len = 0;
  pollfd_setup(&ctx->worker_ctx.pevent_hdr, pfd, &pfd_len);

  // Perform user-provided initialization
  DDRES_CHECK_OR_SHUTDOWN(attr->init_fun(ctx), attr->finish_fun(ctx));

  // Worker poll loop
  while (1) {
    int n = poll(pfd, pfd_len, PSAMPLE_DEFAULT_WAKEUP);

    // If there was an issue, return and let the caller check errno
    if (-1 == n && errno == EINTR) {
      continue;
    } else if (-1 == n) {
      DDRES_CHECK_OR_SHUTDOWN(ddres_error(DD_WHAT_POLLERROR),
                              attr->finish_fun(ctx));
    }

    // If no file descriptors, call time-out
    if (0 == n) {
      DDRes res = ddprof_worker_timeout(continue_profiling, ctx);
      if (IsDDResNotOK(res)) {
        DDRES_CHECK_OR_SHUTDOWN(res, attr->finish_fun(ctx));
      }
      continue;
    }

    int pe_len = ctx->worker_ctx.pevent_hdr.size;
    PEvent *pes = ctx->worker_ctx.pevent_hdr.pes;

    // If we're here, we have at least one file descriptor active.  That means
    // the underyling ringbuffers can be checked.
    for (int i = 0; i < pe_len; i++) {
      if (!pfd[i].revents)
        continue;

      // Even though pollhup might mean that multiple file descriptors (hence,
      // ringbuffers) are still active, in the typical case, `perf_event_open`
      // shuts down either all or nothing.  Accordingly, when it shuts down one
      // file descriptor, we shut down profiling.
      if (pfd[i].revents & POLLHUP) {
        DDRES_GRACEFUL_SHUTDOWN(attr->finish_fun(ctx));
      }

      // Drain the ringbuffer and dispatch to callback, as needed
      // The head and tail are taken literally (without wraparound), since they
      // don't wrap in the underlying object.  Instead, the rb_* interfaces
      // wrap when accessing.
      uint64_t head = pes[i].region->data_head;
      rmb();
      uint64_t tail = pes[i].region->data_tail;
      RingBuffer *rb = &(RingBuffer){0};
      rb_init(rb, pes[i].region, pes[i].reg_size);

      while (head > tail) {
        struct perf_event_header *hdr = rb_seek(rb, tail);
        if ((char *)pes[i].region + pes[i].reg_size < (char *)hdr + hdr->size) {
          // We don't handle the case when the object in the ringbuffer is
          // wrapped around the end.  In such a case, we might copy into a
          // single contiguous object or update the hdr2sample code to account
          // for wrapping, but for now skip it.
        } else {
          DDRes res = ddprof_worker(hdr, pes[i].pos, continue_profiling, ctx);
          if (IsDDResNotOK(res)) {
            // ignoring possible errors from finish as we are closing
            attr->finish_fun(ctx);
            WORKER_SHUTDOWN();
          }
        }
        tail += hdr->size;
      }

      // We tell the kernel how much we read.  This *should* be the same as
      // the current tail, but in the case of an error head will be a safe
      // restart position.
      pes[i].region->data_tail = head;

      if (head != tail)
        LG_WRN("Head/tail buffer mismatch");
    }
  }
}

void main_loop(const WorkerAttr *attr, DDProfContext *ctx) {
  // Setup a shared memory region between the parent and child processes.  This
  // is used to communicate terminal profiling state
  int mmap_prot = PROT_READ | PROT_WRITE;
  int mmap_flags = MAP_ANONYMOUS | MAP_SHARED;
  volatile bool *continue_profiling;
  continue_profiling = mmap(0, sizeof(bool), mmap_prot, mmap_flags, -1, 0);
  if (MAP_FAILED == continue_profiling) {
    // Allocation failure : stop the profiling
    LG_ERR("Could not initialize profiler");
    return;
  }

  // Create worker processes to fulfill poll loop.  Only the parent process
  // can exit with an error code, which signals the termination of profiling.
  if (IsDDResNotOK(spawn_workers(continue_profiling))) {
    return;
  }

  worker(ctx, attr, continue_profiling);
}

void main_loop_lib(const WorkerAttr *attr, DDProfContext *ctx) {
  bool continue_profiling;
  // no fork. TODO : handle lifetime
  worker(ctx, attr, &continue_profiling);
  if (!continue_profiling) {
    LG_NFO("Request to exit");
  }
}
