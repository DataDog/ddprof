// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf_mainloop.hpp"

#include "ddprof_context_lib.hpp"
#include "ddprof_worker.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "logger.hpp"
#include "perf.hpp"
#include "persistent_worker_state.hpp"
#include "pevent.hpp"
#include "unwind.h"

#include <algorithm>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define  DEBUG

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

DDRes spawn_workers(PersistentWorkerState *persistent_worker_state,
                    bool *is_worker) {
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
    if (!persistent_worker_state->restart_worker) {
      if (persistent_worker_state->errors) {
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

//#define POLLIN     0x0001  // EPOLLIN
//#define POLLPRI    0x0002  // EPOLLPRI
//#define POLLOUT    0x0004  // EPOLLOUT
//#define POLLERR    0x0008  // EPOLLERR
//#define POLLHUP    0x0010  // EPOLLHUP
//#define POLLNVAL   0x0020  // unused in epoll
//#define POLLRDNORM 0x0040  // EPOLLRDNORM
//#define POLLRDBAND 0x0080  // EPOLLRDBAND
//#define POLLWRNORM 0x0100  // EPOLLWRNORM
//#define POLLWRBAND 0x0200  // EPOLLWRBAND
//#define POLLMSG    0x0400  // EPOLLMSG
//#define POLLREMOVE 0x1000  // unused in epoll
//#define POLLRDHUP  0x2000  // EPOLLRDHUP


//typedef union epoll_data {
//  void        *ptr;
//  int          fd;
//  uint32_t     u32;
//  uint64_t     u64;
//} epoll_data_t;
//
//struct epoll_event {
//  uint32_t     events;      /* Epoll events */
//  epoll_data_t data;        /* User data variable */
//};

struct EPollEvent {
  struct epoll_event _event;
  int                _fd;
};

static void pollfd_setup(const PEventHdr *pevent_hdr,
                         std::vector<EPollEvent>& epoll_events) {
  // Setup epoll() to watch perf_event file descriptors
  const PEvent *pes = pevent_hdr->pes;
  for (unsigned i = 0; i < pevent_hdr->size; ++i) {
    EPollEvent new_event;
    new_event._event.events = EPOLLIN | EPOLLERR | EPOLLHUP;
    new_event._event.data.u32 = epoll_events.size();
    new_event._fd = pes[i].fd;
    epoll_events.emplace_back(std::move(new_event));
  }
}

static DDRes signalfd_setup(std::vector<EPollEvent>& epoll_events) {
  sigset_t mask;

  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);

  // no need to block signal, since we inherited sigprocmask from parent
  int sfd = signalfd(-1, &mask, 0);
  DDRES_CHECK_ERRNO(sfd, DD_WHAT_WORKERLOOP_INIT, "Could not set signalfd");

  EPollEvent new_event;
  new_event._event.events = EPOLLIN;
  new_event._event.data.u32 = epoll_events.size();
  new_event._fd = sfd;
  epoll_events.emplace_back(std::move(new_event));
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
                         PersistentWorkerState *persistent_worker_state) {

  // Setup poll() to watch perf_event file descriptors
  int pe_len = ctx->worker_ctx.pevent_hdr.size;
  std::vector<EPollEvent> epoll_events;
  pollfd_setup(&ctx->worker_ctx.pevent_hdr, epoll_events);

  DDRES_CHECK_FWD(signalfd_setup(epoll_events));
  int signal_pos = epoll_events.size() - 1;

  // Perform user-provided initialization
  defer { attr->finish_fun(ctx); };
  DDRES_CHECK_FWD(attr->init_fun(ctx, persistent_worker_state));

  bool samples_left_in_buffer = false;
  bool stop = false;

  // Create epoll and register events
  int epoll_fd = epoll_create1(0);
  DDRES_CHECK_INT(epoll_fd, DD_WHAT_POLLERROR, "error opening epoll");
  defer{ close(epoll_fd); };
  int events_length = epoll_events.size();
  for (int i = 0; i != events_length; ++i) {
    DDRES_CHECK_INT(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, epoll_events[i]._fd, &epoll_events[i]._event), DD_WHAT_POLLERROR, "error activating epoll (fd=%d, pos=%d)", epoll_events[i]._fd, i);
  }
  std::vector<struct epoll_event> received_events(events_length);
  // Worker poll loop
  while (!stop) {
    // Convenience structs
    PEvent *pes = ctx->worker_ctx.pevent_hdr.pes;
    if (!samples_left_in_buffer) {
      int event_count = epoll_wait(epoll_fd, received_events.data(),
                                   events_length, PSAMPLE_DEFAULT_WAKEUP_MS);
#ifdef  DEBUG
      std::string event_status(events_length, '0');
#endif
      // If there was an issue, return and let the caller check errno
      if (-1 == event_count && errno == EINTR) {
        continue;
      }
      DDRES_CHECK_ERRNO(event_count, DD_WHAT_POLLERROR, "epoll failed");
      for (int i = 0; i < event_count; ++i) {
        const epoll_event &cur_event = received_events[i];
        int pos = cur_event.data.u32;
        bool is_signal = (pos == signal_pos);
        if (cur_event.events & EPOLLHUP) {
          // hang up
          stop = true;
        }
        if (is_signal && cur_event.events & EPOLLIN) {
          LG_NFO("Received termination signal");
          stop = true;
          break;
        }
        // Assumption is that the first signals are the pevent signals
        if (static_cast<unsigned>(pos) < ctx->worker_ctx.pevent_hdr.size) {
          if (cur_event.events & EPOLLIN && pes[pos].custom_event) {
            // for custom ring buffer, need to read from eventfd to flush POLLIN
            // status
            uint64_t count;
            DDRES_CHECK_ERRNO(read(pes[pos].fd, &count, sizeof(count)),
                              DD_WHAT_PERFRB, "Failed to read from evenfd");
          }
        }
#ifdef DEBUG
        event_status[pos] = '1';
#endif
      }
#ifdef  DEBUG
      LG_NTC("Events ----- %s (%d)", event_status.c_str(), event_count);
#endif
    }

    int64_t now_ns = 0;
    DDRES_CHECK_FWD(worker_process_ring_buffers(pes, pe_len, ctx, &now_ns,
                                                &samples_left_in_buffer));
    DDRES_CHECK_FWD(ddprof_worker_maybe_export(ctx, now_ns));

    if (ctx->worker_ctx.persistent_worker_state->restart_worker) {
      // return directly no need to do a final export
      return {};
    }
  }

  // export current samples before exiting
  DDRES_CHECK_FWD(ddprof_worker_cycle(ctx, 0, true));
  return {};
}

static void worker(DDProfContext *ctx, const WorkerAttr *attr,
                   PersistentWorkerState *persistent_worker_state) {
  persistent_worker_state->restart_worker = false;
  persistent_worker_state->errors = true;

  DDRes res = worker_loop(ctx, attr, persistent_worker_state);
  if (IsDDResFatal(res)) {
    LG_WRN("[PERF] Shut down worker (what:%s).",
           ddres_error_message(res._what));
  } else {
    if (IsDDResNotOK(res)) {
      LG_WRN("Worker warning (what:%s).", ddres_error_message(res._what));
    }
    LG_NTC("Shutting down worker gracefully");
    persistent_worker_state->errors = false;
  }
}

DDRes main_loop(const WorkerAttr *attr, DDProfContext *ctx) {
  // Setup a shared memory region between the parent and child processes.  This
  // is used to communicate terminal profiling state
  int mmap_prot = PROT_READ | PROT_WRITE;
  int mmap_flags = MAP_ANONYMOUS | MAP_SHARED;
  PersistentWorkerState *persistent_worker_state =
      (PersistentWorkerState *)mmap(0, sizeof(PersistentWorkerState), mmap_prot,
                                    mmap_flags, -1, 0);
  if (MAP_FAILED == persistent_worker_state) {
    // Allocation failure : stop the profiling
    LG_ERR("Could not initialize profiler");
    return ddres_error(DD_WHAT_MAINLOOP_INIT);
  }

  defer { munmap(persistent_worker_state, sizeof(*persistent_worker_state)); };

  // Create worker processes to fulfill poll loop.  Only the parent process
  // can exit with an error code, which signals the termination of profiling.
  bool is_worker = false;
  DDRes res = spawn_workers(persistent_worker_state, &is_worker);
  if (IsDDResNotOK(res)) {
    return res;
  }
  if (is_worker) {
    worker(ctx, attr, persistent_worker_state);
    // Ensure worker does not return,
    // because we don't want to free resources (perf_event fds,...) that are
    // shared between processes. Only free the context.
    ddprof_context_free(ctx);
    exit(0);
  }
  return {};
}

void main_loop_lib(const WorkerAttr *attr, DDProfContext *ctx) {
  PersistentWorkerState persistent_worker_state = {};
  // no fork. TODO : give exit trigger to user
  worker(ctx, attr, &persistent_worker_state);
  if (!persistent_worker_state.restart_worker) {
    LG_NFO("Request to exit");
  }
}
