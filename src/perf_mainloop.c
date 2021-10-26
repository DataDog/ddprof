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
#include "producer_linearizer.h"
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

DDRes spawn_workers(volatile bool *can_run) {
  pid_t child_pid;

  while ((child_pid = fork())) {
    LG_WRN("Created child %d", child_pid);
    waitpid(child_pid, NULL, 0);

    // Harvest the exit state of the child process.  We will always reset it
    // to false so that a child who segfaults or exits erroneously does not
    // cause a pointless loop of spawning
    if (!*can_run) {
      DDRES_RETURN_WARN_LOG(DD_WHAT_MAINLOOP, "Stop profiling");
    } else {
      *can_run = false;
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
  for (int i = 0; i < *pfd_len; ++i) {
    // NOTE: if fd is negative, it will be ignored
    pfd[i].fd = pes[i].fd;
    pfd[i].events = POLLIN | POLLERR | POLLHUP;
  }
}

static bool dispatch_event(DDProfContext *ctx, struct perf_event_header *hdr,
                           int pos, const WorkerAttr *attr,
                           volatile bool *can_run) {
  // Processes the given event
  DDRes res = ddprof_worker(hdr, pos, can_run, ctx);
  if (IsDDResNotOK(res))
    return false;
  return true;
}

static void worker(DDProfContext *ctx, const WorkerAttr *attr,
                   volatile bool *can_run) {
  // Until a restartable terminal condition is met, the worker will set its
  // disposition so that profiling is halted upon its termination
  *can_run = false;

  // Setup poll() to watch perf_event file descriptors
  struct pollfd pfd[MAX_NB_WATCHERS];
  int pfd_len = 0;
  pollfd_setup(&ctx->worker_ctx.pevent_hdr, pfd, &pfd_len);

  // Perform user-provided initialization
  DDRES_CHECK_OR_SHUTDOWN(attr->init_fun(ctx), attr->finish_fun(ctx));

  // Setup a ProducerLinearizer for managing the ringbuffer
  int pe_len = ctx->worker_ctx.pevent_hdr.size;
  ProducerLinearizer pl = {0};
  uint64_t *time_values = calloc(sizeof(uint64_t), pe_len);
  if (!time_values)
    WORKER_SHUTDOWN();
  uint64_t i_ev;
  if (!ProducerLinearizer_init(&pl, pe_len, time_values)) {
    free(time_values);
    WORKER_SHUTDOWN();
  }

  // Setup array to track headers, so we don't need to re-copy elements
  struct perf_event_header **hdrs = calloc(sizeof(*hdrs), pe_len);
  if (!hdrs) {
    free(time_values);
    WORKER_SHUTDOWN();
  }

  // Worker poll loop
  while (1) {
    int n = poll(pfd, pfd_len, PSAMPLE_DEFAULT_WAKEUP);
    int processed_samples = 0;

    // If there was an issue, return and let the caller check errno
    if (-1 == n && errno == EINTR) {
      continue;
    } else if (-1 == n) {
      DDRES_CHECK_OR_SHUTDOWN(ddres_error(DD_WHAT_POLLERROR),
                              attr->finish_fun(ctx));
    }

    // Convenience structs
    PEvent *pes = ctx->worker_ctx.pevent_hdr.pes;

    // If one of the perf_event_open() feeds was closed by the kernel, shut down
    // profiling
    for (int i = 0; i < pe_len; ++i) {
      // Even though pollhup might mean that multiple file descriptors (hence,
      // ringbuffers) are still active, in the typical case, `perf_event_open`
      // shuts down either all or nothing.  Accordingly, when it shuts down one
      // file descriptor, we shut down profiling.
      if (pfd[i].revents & POLLHUP)
        DDRES_GRACEFUL_SHUTDOWN(attr->finish_fun(ctx));
    }

    // While there are events to process, iterate through them.  This strategy
    // orders the current topmost event from each watcher, which assumes that
    // within a single watcher conflicting events are ordered properly.  This
    // seems to be a valid assumption on x86/ARM.
    while (1) {
      for (int i = 0; i < pe_len; ++i) {
        // If a watcher is not free, then we already have the oldest time.
        // Skip it.
        if (!pl.F[i])
          continue;

        // Memory-ordering safe access of ringbuffer elements
        RingBuffer *rb = &pes[i].rb;
        uint64_t head = rb->region->data_head;
        rmb();
        uint64_t tail = rb->region->data_tail;

        if (head > tail) {
          hdrs[i] = rb_seek(rb, tail);
          time_values[i] = hdr_time(hdrs[i], DEFAULT_SAMPLE_TYPE);
          ProducerLinearizer_push(&pl, i);
        }
      }

      // We've iterated through the ringbuffers, populating the
      // ProducerLinearizer with any new events.  Try to pop one event.  If none
      // are available, then return to `poll()` and wait.
      if (!ProducerLinearizer_pop(&pl, &i_ev))
        break;

      // At this point in time, we've identified the event we're going to
      // process.  We advance the corresponding ringbuffer so we do not
      // revisit that event again
      pes[i_ev].rb.region->data_tail += hdrs[i_ev]->size;

      // Attempt to dispatch the event
      if (!dispatch_event(ctx, hdrs[i_ev], pes[i_ev].pos, attr, can_run)) {
        // If dispatch failed, we discontinue the worker
        attr->finish_fun(ctx);
        free(time_values);
        free(hdrs);
        WORKER_SHUTDOWN();
      } else {
        // Otherwise, we successfully dispatched an event.  If it was a sample,
        // then say so
        if (hdrs[i_ev]->type == PERF_RECORD_SAMPLE)
          ++processed_samples;
      }
    }

    // If I didn't process any events, then hit the timeout
    if (!processed_samples) {
      DDRes res = ddprof_worker_timeout(can_run, ctx);
      if (IsDDResNotOK(res)) {
        attr->finish_fun(ctx);
        DDRES_CHECK_OR_SHUTDOWN(res, attr->finish_fun(ctx));
      }
    }
  }
}

void main_loop(const WorkerAttr *attr, DDProfContext *ctx) {
  // Setup a shared memory region between the parent and child processes.  This
  // is used to communicate terminal profiling state
  int mmap_prot = PROT_READ | PROT_WRITE;
  int mmap_flags = MAP_ANONYMOUS | MAP_SHARED;
  volatile bool *can_run;
  can_run = mmap(0, sizeof(bool), mmap_prot, mmap_flags, -1, 0);
  if (MAP_FAILED == can_run) {
    // Allocation failure : stop the profiling
    LG_ERR("Could not initialize profiler");
    return;
  }

  // Create worker processes to fulfill poll loop.  Only the parent process
  // can exit with an error code, which signals the termination of profiling.
  if (IsDDResNotOK(spawn_workers(can_run))) {
    return;
  }

  worker(ctx, attr, can_run);
}

void main_loop_lib(const WorkerAttr *attr, DDProfContext *ctx) {
  bool can_run;
  // no fork. TODO : handle lifetime
  worker(ctx, attr, &can_run);
  if (!can_run) {
    LG_NFO("Request to exit");
  }
}
