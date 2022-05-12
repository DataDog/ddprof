// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf_mainloop.h"

#include <algorithm>
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
#include "ddprof_context_lib.h"
#include "ddprof_worker.h"
#include "ddres.h"
#include "logger.h"
#include "perf.h"
#include "pevent.h"
#include "unwind.h"
}

#include "defer.hpp"

static pid_t g_child_pid = 0;
static bool g_termination_requested = false;

static inline int64_t now_nanos() {
  static struct timeval tv = {};
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000000 + tv.tv_usec) * 1000;
}

static void handle_signal(int) {
  g_termination_requested = true;

  // forwarding signal to child
  if (g_child_pid) {
    kill(g_child_pid, SIGTERM);
  }
}

static DDRes install_signal_handler() {
  sigset_t sigset;
  struct sigaction sa;
  DDRES_CHECK_ERRNO(sigemptyset(&sigset), DD_WHAT_MAINLOOP_INIT,
                    "sigemptyset failed");
  sa.sa_handler = &handle_signal;
  sa.sa_mask = sigset;
  sa.sa_flags = SA_RESTART;
  DDRES_CHECK_ERRNO(sigaction(SIGTERM, &sa, NULL), DD_WHAT_MAINLOOP_INIT,
                    "Setting SIGTERM handler failed");
  DDRES_CHECK_ERRNO(sigaction(SIGINT, &sa, NULL), DD_WHAT_MAINLOOP_INIT,
                    "Setting SIGINT handler failed");
  return {};
}

static void modify_sigprocmask(int how) {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  sigprocmask(how, &mask, NULL);
}

DDRes spawn_workers(MLWorkerFlags *flags, bool *is_worker) {
  pid_t child_pid = 0;
  *is_worker = false;

  DDRES_CHECK_FWD(install_signal_handler());

  // block signals to avoid a race condition between checking
  // g_termination_requested flag and fork/waitpid
  modify_sigprocmask(SIG_BLOCK);

  // child immediately exits the while() and returns from this function, whereas
  // the parent stays here forever, spawning workers.
  while (!g_termination_requested && (child_pid = fork())) {
    g_child_pid = child_pid;
    {
      LG_NTC("Created child %d", child_pid);
      // unblock signals, we can now forward signals to child
      modify_sigprocmask(SIG_UNBLOCK);
      waitpid(g_child_pid, NULL, 0);
    }

    g_child_pid = 0;

    // Harvest the exit state of the child process.  We will always reset it
    // to false so that a child who segfaults or exits erroneously does not
    // cause a pointless loop of spawning
    if (!flags->restart_worker) {
      if (flags->errors) {
        DDRES_RETURN_WARN_LOG(DD_WHAT_MAINLOOP, "Stop profiling");
      } else {
        break;
      }
    }
    LG_NFO("Refreshing worker process");
  }

  *is_worker = child_pid == 0;
  return {};
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

static DDRes signalfd_setup(pollfd *pfd) {
  sigset_t mask;

  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);

  // no need to block signal, since we inherited sigprocmask from parent
  int sfd = signalfd(-1, &mask, 0);
  DDRES_CHECK_ERRNO(sfd, DD_WHAT_WORKERLOOP_INIT, "Could not set signalfd");

  pfd->fd = sfd;
  pfd->events = POLLIN;
  return {};
}

static inline DDRes worker_process_ring_buffers(PEvent *pes, int pe_len,
                                                DDProfContext *ctx,
                                                int64_t *now_ns,
                                                bool *timeout) {
  // While there are events to process, iterate through them
  // while limiting time spent in loop to at most PSAMPLE_DEFAULT_WAKEUP_MS
  int64_t loop_start_ns = now_nanos();
  int64_t local_now_ns;

  bool events;
  do {
    events = false;
    for (int i = 0; i < pe_len; ++i) {
      RingBuffer *rb = &pes[i].rb;

      // Memory-ordering safe access of ringbuffer elements:
      // https://github.com/torvalds/linux/blob/v5.16/tools/include/linux/ring_buffer.h#L59
      uint64_t head = __atomic_load_n(&rb->region->data_head, __ATOMIC_ACQUIRE);
      uint64_t tail = rb->region->data_tail;
      while (tail != head) {
        events = true;

        // Attempt to dispatch the event
        struct perf_event_header *hdr = rb_seek(rb, tail);
        assert(hdr->size > 0);
        DDRes res = ddprof_worker_process_event(hdr, pes[i].watcher_pos, ctx);

        // We've processed the current event, so we can advance the ringbuffer
        tail += hdr->size;

        // Check for processing error
        if (IsDDResNotOK(res)) {
          __atomic_store_n(&rb->region->data_tail, tail, __ATOMIC_RELEASE);
          return res;
        }
      }

      if (events) {
        __atomic_store_n(&rb->region->data_tail, tail, __ATOMIC_RELEASE);
      }
    }

    local_now_ns = now_nanos();
  } while (events &&
           (local_now_ns - loop_start_ns) <
               PSAMPLE_DEFAULT_WAKEUP_MS * 1000000L);

  *now_ns = local_now_ns;
  *timeout = events;
  return {};
}

static DDRes worker_loop(DDProfContext *ctx, const WorkerAttr *attr,
                         bool *restart_worker) {

  // Setup poll() to watch perf_event file descriptors
  int pe_len = ctx->worker_ctx.pevent_hdr.size;
  // one extra slot in pfd to accomodate for signal fd
  struct pollfd pfds[MAX_NB_WATCHERS + 1];
  int pfd_len = 0;
  pollfd_setup(&ctx->worker_ctx.pevent_hdr, pfds, &pfd_len);

  DDRES_CHECK_FWD(signalfd_setup(&pfds[pfd_len]));
  int signal_pos = pfd_len++;

  // Perform user-provided initialization
  defer { attr->finish_fun(ctx); };
  DDRES_CHECK_FWD(attr->init_fun(ctx));

  bool samples_left_in_buffer = false;
  bool stop = false;

  // Worker poll loop
  while (!stop) {
    // Convenience structs
    PEvent *pes = ctx->worker_ctx.pevent_hdr.pes;

    if (!samples_left_in_buffer) {
      int n = poll(pfds, pfd_len, PSAMPLE_DEFAULT_WAKEUP_MS);

      // If there was an issue, return and let the caller check errno
      if (-1 == n && errno == EINTR) {
        continue;
      }
      DDRES_CHECK_ERRNO(n, DD_WHAT_POLLERROR, "poll failed");

      if (pfds[signal_pos].revents & POLLIN) {
        LG_NFO("Received termination signal");
        break;
      }

      for (int i = 0; i < pe_len; ++i) {
        pollfd &pfd = pfds[i];
        if (pfd.revents & POLLHUP) {
          stop = true;
        } else if (pfd.revents & POLLIN && pes[i].custom_event) {
          // for custom ring buffer, need to read from eventfd to flush POLLIN
          // status
          uint64_t count;
          DDRES_CHECK_ERRNO(read(pes[i].fd, &count, sizeof(count)),
                            DD_WHAT_PERFRB, "Failed to read from evenfd");
        }
      }
    }

    int64_t now_ns = 0;
    DDRES_CHECK_FWD(worker_process_ring_buffers(pes, pe_len, ctx, &now_ns,
                                                &samples_left_in_buffer));
    DDRES_CHECK_FWD(ddprof_worker_maybe_export(ctx, now_ns, restart_worker));

    if (*restart_worker) {
      // return directly no need to do a final export
      return {};
    }
  }

  // export current samples before exiting
  DDRES_CHECK_FWD(ddprof_worker_cycle(ctx, 0, true));
  return {};
}

static void worker(DDProfContext *ctx, const WorkerAttr *attr,
                   MLWorkerFlags *flags) {
  bool restart_worker = false;
  flags->restart_worker = false;
  flags->errors = true;

  DDRes res = worker_loop(ctx, attr, &restart_worker);
  if (IsDDResFatal(res)) {
    LG_WRN("[PERF] Shut down worker (what:%s).",
           ddres_error_message(res._what));
  } else {
    if (IsDDResNotOK(res)) {
      LG_WRN("Worker warning (what:%s).", ddres_error_message(res._what));
    }
    LG_NFO("Shutting down worker gracefully");
    flags->restart_worker = restart_worker;
    flags->errors = false;
  }
}

DDRes main_loop(const WorkerAttr *attr, DDProfContext *ctx) {
  // Setup a shared memory region between the parent and child processes.  This
  // is used to communicate terminal profiling state
  int mmap_prot = PROT_READ | PROT_WRITE;
  int mmap_flags = MAP_ANONYMOUS | MAP_SHARED;
  MLWorkerFlags *flags =
      (MLWorkerFlags *)mmap(0, sizeof(*flags), mmap_prot, mmap_flags, -1, 0);
  if (MAP_FAILED == flags) {
    // Allocation failure : stop the profiling
    LG_ERR("Could not initialize profiler");
    return ddres_error(DD_WHAT_MAINLOOP_INIT);
  }

  defer { munmap(flags, sizeof(*flags)); };

  // Create worker processes to fulfill poll loop.  Only the parent process
  // can exit with an error code, which signals the termination of profiling.
  bool is_worker = false;
  DDRes res = spawn_workers(flags, &is_worker);
  if (IsDDResNotOK(res)) {
    return res;
  }
  if (is_worker) {
    worker(ctx, attr, flags);
    // Ensure worker does not return,
    // because we don't want to free resources (perf_event fds,...) that are
    // shared between processes. Only free the context.
    ddprof_context_free(ctx);
    exit(0);
  }
  return {};
}

void main_loop_lib(const WorkerAttr *attr, DDProfContext *ctx) {
  MLWorkerFlags flags = {};
  // no fork. TODO : give exit trigger to user
  worker(ctx, attr, &flags);
  if (!flags.restart_worker) {
    LG_NFO("Request to exit");
  }
}
