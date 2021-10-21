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
    LG_WRN("[PERF] Shut down worker (what:%s).",                               \
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
    bool sample_hit = false;

    // If there was an issue, return and let the caller check errno
    if (-1 == n && errno == EINTR) {
      continue;
    } else if (-1 == n) {
      DDRES_CHECK_OR_SHUTDOWN(ddres_error(DD_WHAT_POLLERROR),
                              attr->finish_fun(ctx));
    }

    int pe_len = ctx->worker_ctx.pevent_hdr.size;
    PEvent *pes = ctx->worker_ctx.pevent_hdr.pes;

    // Check the ringbuffers in order
    for (int i = 0; i < pe_len; i++) {
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
      RingBuffer *rb = &pes[i].rb;
      struct perf_event_mmap_page *perfpage = rb->region;
      uint64_t head = perfpage->data_head;
      rmb();
      uint64_t tail = perfpage->data_tail;

      while (head > tail) {
        struct perf_event_header *hdr = rb_seek(rb, tail);

        // If the current element wraps around the buffer, need change hdr to
        // point to a linearized copy of the element, since the processors
        // don't handle overflow.  rb->size is actually the ringbuffer plus
        // the metadata page, so make sure to account for that properly.
        uint64_t rb_size = rb->size - rb->meta_size;

        // The terms 'left' and 'right' below refer to the regions in the
        // linearized buffer.  In the index space of the ringbuffer, these terms
        // would be reversed.
        if (rb_size - rb->offset < hdr->size) {
          unsigned char *wrbuf = rb->wrbuf;
          uint64_t left_sz = rb_size - rb->offset;
          uint64_t right_sz = hdr->size - left_sz;
          memcpy(wrbuf, rb->start + rb->offset, left_sz);
          memcpy(wrbuf + left_sz, rb->start, right_sz);
          hdr = (struct perf_event_header *)wrbuf;
        }

        // TODO this strongly binds the behavior of the sample processor as an
        // export interface to this dispatch layer.  We can overcome this by
        // moving the ddprof export code and adding a constant-time callback,
        // but for now we're keeping this hack.
        if (hdr->type == PERF_RECORD_SAMPLE)
          sample_hit = true;

        // Pass the event to the processors
        DDRes res = ddprof_worker(hdr, pes[i].pos, continue_profiling, ctx);
        if (IsDDResNotOK(res)) {
          // ignoring possible errors from finish as we are closing
          attr->finish_fun(ctx);
          WORKER_SHUTDOWN();
        }
        tail += hdr->size;
      }

      // We tell the kernel how much we read.  This *should* be the same as
      // the current tail, but in the case of an error head will be a safe
      // restart position.
      perfpage->data_tail = head;

      if (head != tail)
        LG_WRN("Head/tail buffer mismatch");
    }

    // If I didn't process any events, then hit the timeout
    if (sample_hit) {
      DDRes res = ddprof_worker_timeout(continue_profiling, ctx);
      if (IsDDResNotOK(res)) {
        attr->finish_fun(ctx);
        DDRES_CHECK_OR_SHUTDOWN(res, attr->finish_fun(ctx));
      }
      continue;
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
