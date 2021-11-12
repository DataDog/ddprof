// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

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

#include "ddprof_context_lib.h"
#include "ddprof_worker.h"
#include "ddres.h"
#include "logger.h"
#include "perf.h"
#include "pevent.h"
#include "unwind.h"

#define rmb() __asm__ volatile("lfence" ::: "memory")

#define WORKER_SHUTDOWN()                                                      \
  { return; }

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

static void worker(DDProfContext *ctx, const WorkerAttr *attr,
                   volatile bool *can_run) {
  // Until a restartable terminal condition is met, the worker will set its
  // disposition so that profiling is halted upon its termination
  *can_run = false;

  // Setup poll() to watch perf_event file descriptors
  int pe_len = ctx->worker_ctx.pevent_hdr.size;
  struct pollfd pfd[MAX_NB_WATCHERS];
  int pfd_len = 0;
  pollfd_setup(&ctx->worker_ctx.pevent_hdr, pfd, &pfd_len);

  // Perform user-provided initialization
  DDRES_CHECK_OR_SHUTDOWN(attr->init_fun(ctx), attr->finish_fun(ctx));

  // Worker poll loop
  while (1) {
    uint64_t processed_samples = 0;
    int n = poll(pfd, pfd_len, PSAMPLE_DEFAULT_WAKEUP);

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
      // This closes profiling when there still may be viable feeds, but we
      // don't handle that case yet.
      if (pfd[i].revents & POLLHUP) {
        DDRES_GRACEFUL_SHUTDOWN(attr->finish_fun(ctx));
      }
    }

    // While there are events to process, iterate through them.
    bool events;
    do {
      events = false;
      for (int i = 0; i < pe_len; ++i) {
        // Memory-ordering safe access of ringbuffer elements
        RingBuffer *rb = &pes[i].rb;
        uint64_t head = rb->region->data_head;
        rmb();
        uint64_t tail = rb->region->data_tail;
        if (head == tail)
          continue;
        events = true;

        // Attempt to dispatch the event
        struct perf_event_header *hdr = rb_seek(rb, tail);
        DDRes res = ddprof_worker(hdr, pes[i].pos, can_run, ctx);

        // We've processed the current event, so we can advance the ringbuffer
        rb->region->data_tail += hdr->size;

        // Check for processing error
        if (IsDDResNotOK(res)) {
          attr->finish_fun(ctx);
          WORKER_SHUTDOWN();
        } else {
          if (hdr->type == PERF_RECORD_SAMPLE)
            ++processed_samples;
        }
      }
    } while (events);

    // If I didn't process any events, then hit the timeout
    if (!processed_samples) {
      DDRes res = ddprof_worker_timeout(can_run, ctx);
      if (IsDDResNotOK(res)) {
        attr->finish_fun(ctx);
        WORKER_SHUTDOWN();
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
  // Child finished profiling: Free and exit
  ddprof_context_free(ctx);
  exit(0);
}

void main_loop_lib(const WorkerAttr *attr, DDProfContext *ctx) {
  bool can_run;
  // no fork. TODO : give exit trigger to user
  worker(ctx, attr, &can_run);
  if (!can_run) {
    LG_NFO("Request to exit");
  }
}
