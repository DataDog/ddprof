// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf_mainloop.hpp"

#include "ddprof_context_lib.hpp"
#include "ddprof_worker.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "ipc.hpp"
#include "logger.hpp"
#include "perf.hpp"
#include "persistent_worker_state.hpp"
#include "pevent.hpp"
#include "ringbuffer_utils.hpp"
#include "unique_fd.hpp"
#include "unwind.h"
#include "unwind_state.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <queue>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <variant>

namespace ddprof {
namespace {

pid_t g_child_pid = 0;
std::atomic<bool> g_termination_requested{false};

void handle_signal(int /*unused*/) {
  g_termination_requested.store(true, std::memory_order::relaxed);

  // forwarding signal to child
  if (g_child_pid) {
    kill(g_child_pid, SIGTERM);
  }
}

DDRes install_signal_handler() {
  sigset_t sigset;
  struct sigaction sa;
  DDRES_CHECK_ERRNO(sigemptyset(&sigset), DD_WHAT_MAINLOOP_INIT,
                    "sigemptyset failed");
  sa.sa_handler = &handle_signal;
  sa.sa_mask = sigset;
  sa.sa_flags = SA_RESTART;
  DDRES_CHECK_ERRNO(sigaction(SIGTERM, &sa, nullptr), DD_WHAT_MAINLOOP_INIT,
                    "Setting SIGTERM handler failed");
  DDRES_CHECK_ERRNO(sigaction(SIGINT, &sa, nullptr), DD_WHAT_MAINLOOP_INIT,
                    "Setting SIGINT handler failed");
  return {};
}

void modify_sigprocmask(int how) {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  sigprocmask(how, &mask, nullptr);
}

DDRes spawn_workers(PersistentWorkerState *persistent_worker_state,
                    bool *is_worker) {
  *is_worker = false;

  DDRES_CHECK_FWD(install_signal_handler());

  // child immediately exits the while() and returns from this function, whereas
  // the parent stays here forever, spawning workers.
  while (!g_termination_requested.load(std::memory_order::relaxed)) {

    // block signals to avoid a race condition between checking
    // g_termination_requested flag and fork/waitpid
    modify_sigprocmask(SIG_BLOCK);
    g_child_pid = fork();
    // unblock signals
    modify_sigprocmask(SIG_UNBLOCK);

    if (!g_child_pid) {
      // worker process
      *is_worker = true;
      break;
    }

    LG_NTC("Created child %d", g_child_pid);
    waitpid(g_child_pid, nullptr, 0);
    g_child_pid = 0;

    // Harvest the exit state of the child process.  We will always reset it
    // to false so that a child who segfaults or exits erroneously does not
    // cause a pointless loop of spawning.
    if (!persistent_worker_state->restart_worker) {
      if (persistent_worker_state->errors) {
        DDRES_RETURN_WARN_LOG(DD_WHAT_MAINLOOP, "Stop profiling");
      } else {
        break;
      }
    }
    LG_NFO("Refreshing worker process");
  }

  return {};
}

ReplyMessage create_reply_message(const DDProfContext &ctx) {
  ReplyMessage reply;
  reply.request = RequestMessage::kProfilerInfo;
  reply.pid = getpid();

  int alloc_watcher_idx = context_allocation_profiling_watcher_idx(ctx);
  if (alloc_watcher_idx != -1) {
    std::span const pevents{ctx.worker_ctx.pevent_hdr.pes,
                            ctx.worker_ctx.pevent_hdr.size};
    auto event_it =
        std::find_if(pevents.begin(), pevents.end(),
                     [alloc_watcher_idx](const auto &pevent) {
                       return pevent.watcher_pos == alloc_watcher_idx;
                     });
    if (event_it != pevents.end()) {
      reply.ring_buffer.event_fd = event_it->fd;
      reply.ring_buffer.ring_fd = event_it->mapfd;
      reply.ring_buffer.mem_size = event_it->ring_buffer_size;
      reply.ring_buffer.ring_buffer_type =
          static_cast<int>(event_it->ring_buffer_type);
      reply.allocation_profiling_rate =
          ctx.watchers[alloc_watcher_idx].sample_period;
      reply.stack_sample_size =
          ctx.watchers[alloc_watcher_idx].options.stack_sample_size;
      reply.initial_loaded_libs_check_delay_ms =
          ctx.params.initial_loaded_libs_check_delay.count();
      reply.loaded_libs_check_interval_ms =
          ctx.params.loaded_libs_check_interval.count();

      if (Any(ctx.watchers[alloc_watcher_idx].aggregation_mode &
              EventAggregationMode::kLiveSum)) {
        reply.allocation_flags |= ReplyMessage::kLiveSum;
      }
    }
  }

  return reply;
}

void pollfd_setup(std::span<PEvent> pes, struct pollfd *pfd) {
  // Setup poll() to watch perf_event file descriptors
  for (size_t i = 0; i < pes.size(); ++i) {
    // NOTE: if fd is negative, it will be ignored
    pfd[i].fd = pes[i].fd;
    pfd[i].events = POLLIN;
  }
}

// EventWrapper holds a reference to a perf_event_header with its associated
// timestamp.
// It is used to order events in a std::priority_queue without copying events.
// perf_event_header is not owned by EventWrapper, it points on ring buffer
// memory, and must remain valid during the lifetime of EventWrapper.
// Consequently, care must be taken to advance reader cursor position in ring
// buffer only after processing the event.
struct EventWrapper {
  const perf_event_header *event;
  PerfClock::time_point timestamp;
  int buffer_idx;

  friend bool operator>(const EventWrapper &lhs, const EventWrapper &rhs) {
    return lhs.timestamp > rhs.timestamp;
  }
};

using EventQueue = std::priority_queue<EventWrapper, std::vector<EventWrapper>,
                                       std::greater<EventWrapper>>;

DDRes worker_process_ring_buffers_ordered(std::span<PEvent> pes,
                                          DDProfContext &ctx,
                                          EventQueue &event_queue, bool drain) {
  // Reorder events from ring buffers before processing them.
  // Events in each perf ring buffer are already ordered by timestamp.
  // For MPSC ring buffers, there is no such guarantee.
  // The strategy is to dequeue events from ring buffers and push them in a
  // priority queue, ordered by timestamp. And then process this priority queue.
  // When a ring buffer is empty, we cannot be sure that a new event with a
  // timestamp less than a previously enqueued event will not be added to the
  // ring buffer in the future.
  // That's why we arbitrarily define a maximum latency `kMaxSampleLatency`
  // between the timestamp of an event and the time it appears in the ring
  // buffer, and we process events with timestamps up to (now -
  // kMaxSampleLatency).
  // For MPSC ring buffers, we dequeue events and push them into the priority
  // queue until we reach an event with a timestamp greater than (now -
  // kMaxSampleLatency) or the ring buffer is empty. Note that when an event
  // with a timestamp greater than (now - kMaxSampleLatency) is dequeued, it is
  // still pushed in the priority_queue.
  // For perf ring buffers, we ensure that at anytime at most one event from
  // each ring buffer is in the priority queue. This ensures that events with
  // identical timestamps in a ring buffer are processed in the same order as in
  // the ring buffer (priority queue is not stable, therefore pushing events
  // with identical timestamps might permute them) and this also makes advancing
  // the reader cursor position in the ring buffer easier since know that only
  // one event has been read, we can just bump the reader cursor to the last
  // read position.
  // When an event from a perf ring buffer is dequeued fron the priority queue,
  // we advance the reader cursor position in the ring buffer to free the slot
  // for the writer, and attempt to read the next event from the ring buffer if
  // not empty.
  // When an event from a MPSC ring buffer is dequeued from the priority queue,
  // we try to advance the reader cursor position in the ring buffer to free the
  // slot for the writer. Since events might be out of order for this ring
  // buffer, advancing is done by marking processed events as discarded and
  // bumping the reader position until empty or we reach the first non-discarded
  // event.

  const std::chrono::microseconds kMaxSampleLatency{100};

  auto now = PerfClock::now();
  auto deadline =
      drain ? PerfClock::time_point::max() : now + k_sample_default_wakeup;

  while (!g_termination_requested.load(std::memory_order::relaxed) &&
         now <= deadline) {
    auto max_timestamp =
        drain ? PerfClock::time_point::max() : now - kMaxSampleLatency;
    int new_events = 0;

    // Dequeue events from each ring buffer and push them in the priority queue
    for (int i = 0; i < static_cast<int>(pes.size()); ++i) {
      auto &rb = pes[i].rb;

      if (rb.type == RingBufferType::kPerfRingBuffer) {
        // if perf ring buffer has already events in the priority queue, skip it
        if (!perf_rb_has_inflight_events(rb)) {
          const perf_event_header *event = perf_rb_read_event(rb);
          if (event) {
            auto timestamp = perf_clock_time_point_from_timestamp(
                hdr_time(event, ctx.watchers[pes[i].watcher_pos].sample_type));
            event_queue.push({event, timestamp, i});
            ++new_events;
          }
        }
      } else {
        while (true) {
          const perf_event_header *event = mpsc_rb_read_event(rb);
          if (!event) {
            break;
          }

          auto timestamp = perf_clock_time_point_from_timestamp(
              hdr_time(event, ctx.watchers[pes[i].watcher_pos].sample_type));
          event_queue.push({event, timestamp, i});
          ++new_events;
          if (timestamp > max_timestamp) {
            break;
          }
        }
      }
    }

    while (!event_queue.empty()) {
      const auto &evt = event_queue.top();
      if (evt.timestamp > max_timestamp) {
        // the next event is too recent, stop processing
        return {};
      }
      auto &pevent = pes[evt.buffer_idx];
      auto res =
          ddprof_worker_process_event(evt.event, pevent.watcher_pos, ctx);
      if (!IsDDResOK(res)) {
        return res;
      }

      auto &rb = pevent.rb;
      if (rb.type == RingBufferType::kPerfRingBuffer) {
        // advance ring buffer, this frees space for the writer end
        perf_rb_advance(rb);

        const perf_event_header *new_event = perf_rb_read_event(rb);
        if (new_event) {
          // enqueue next event from the same ring buffer
          auto timestamp = perf_clock_time_point_from_timestamp(hdr_time(
              new_event, ctx.watchers[pevent.watcher_pos].sample_type));
          event_queue.push({new_event, timestamp, evt.buffer_idx});
        }
      } else {
        // advance ring buffer if possible, this frees space for the writer end
        mpsc_rb_advance_if_possible(rb, evt.event);
      }
      event_queue.pop();
    }

    if (!new_events) {
      // no new events were queued, stop processing
      return {};
    }

    now = PerfClock::now();
  }

  return {};
}

inline DDRes
worker_process_ring_buffers(std::span<PEvent> pes, DDProfContext &ctx,
                            std::chrono::steady_clock::time_point *now) {
  // While there are events to process, iterate through them
  // while limiting time spent in loop to at most k_sample_default_wakeup
  auto loop_start = std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point local_now;

  bool events;
  do {
    events = false;
    for (auto &pevent : pes) {
      auto &ring_buffer = pevent.rb;
      if (ring_buffer.type == RingBufferType::kPerfRingBuffer) {
        PerfRingBufferReader reader(&ring_buffer);

        ConstBuffer buffer = reader.read_all_available();
        while (!buffer.empty()) {
          const auto *hdr =
              reinterpret_cast<const perf_event_header *>(buffer.data());
          DDRes res = ddprof_worker_process_event(hdr, pevent.watcher_pos, ctx);

          // Check for processing error
          if (IsDDResNotOK(res)) {
            return res;
          }
          // \fixme{nsavoire} free slot as soon as possible ?
          // reader.advance(hdr->size);

          buffer = remaining(buffer, hdr->size);
        }
      } else {
        MPSCRingBufferReader reader{&ring_buffer};
        for (ConstBuffer buffer{reader.read_sample()}; !buffer.empty();
             buffer = reader.read_sample()) {
          const auto *hdr =
              reinterpret_cast<const perf_event_header *>(buffer.data());
          DDRes res = ddprof_worker_process_event(hdr, pevent.watcher_pos, ctx);

          // Check for processing error
          if (IsDDResNotOK(res)) {
            return res;
          }

          // \fixme{nsavoire} free slot as soon as possible ?
          // reader.advance();
        }
      }

      // PerfRingBufferReader destructor takes care of advancing ring buffer
      // read position
    }
    local_now = std::chrono::steady_clock::now();
  } while (events && (local_now - loop_start) < k_sample_default_wakeup);

  *now = local_now;
  return {};
}

DDRes worker_loop(DDProfContext &ctx, const WorkerAttr *attr,
                  PersistentWorkerState *persistent_worker_state) {

  // Setup poll() to watch perf_event file descriptors
  pollfd poll_fds[k_max_nb_perf_event_open];
  std::span const pevents{ctx.worker_ctx.pevent_hdr.pes,
                          ctx.worker_ctx.pevent_hdr.size};
  pollfd_setup(pevents, poll_fds);

  // Perform user-provided initialization
  defer { attr->finish_fun(ctx); };
  DDRES_CHECK_FWD(attr->init_fun(ctx, persistent_worker_state));

  if (ctx.params.pid > 0 && ctx.backpopulate_pid_upon_start &&
      persistent_worker_state->profile_seq == 0) {
    int nb_elems;
    ctx.worker_ctx.us->dso_hdr.pid_backpopulate(ctx.params.pid, nb_elems);
  }

  WorkerServer const server =
      start_worker_server(ctx.socket_fd.get(), create_reply_message(ctx));

  EventQueue event_queue;
  // Worker poll loop
  while (!g_termination_requested.load(std::memory_order::relaxed)) {
    int const n =
        poll(poll_fds, pevents.size(),
             std::chrono::milliseconds{k_sample_default_wakeup}.count());

    // If there was an issue, return and let the caller check errno
    if (-1 == n && errno == EINTR) {
      continue;
    }
    DDRES_CHECK_ERRNO(n, DD_WHAT_POLLERROR, "poll failed");

    bool stop = false;
    for (size_t i = 0; i < pevents.size(); ++i) {
      pollfd const &pfd = poll_fds[i];
      if (pfd.revents & POLLHUP) {
        stop = true;
      } else if (pfd.revents & POLLIN && pevents[i].custom_event) {
        // for custom ring buffer, need to read from eventfd to flush POLLIN
        // status
        uint64_t count;
        DDRES_CHECK_ERRNO(read(pevents[i].fd, &count, sizeof(count)),
                          DD_WHAT_PERFRB, "Failed to read from evenfd");
      }
    }

    std::chrono::steady_clock::time_point now;
    if (ctx.params.reorder_events) {
      DDRES_CHECK_FWD(
          worker_process_ring_buffers_ordered(pevents, ctx, event_queue, stop));
      now = std::chrono::steady_clock::now();
    } else {
      DDRES_CHECK_FWD(worker_process_ring_buffers(pevents, ctx, &now));
    }

    DDRES_CHECK_FWD(ddprof_worker_maybe_export(ctx, now));

    if (ctx.worker_ctx.persistent_worker_state->restart_worker) {
      // return directly no need to do a final export
      return {};
    }

    if (stop) {
      break;
    }
  }

  // export current samples before exiting
  DDRES_CHECK_FWD(ddprof_worker_cycle(ctx, {}, true));
  return {};
}

void worker(DDProfContext &ctx, const WorkerAttr *attr,
            PersistentWorkerState *persistent_worker_state) {
  persistent_worker_state->restart_worker = false;
  persistent_worker_state->errors = true;

  DDRes const res = worker_loop(ctx, attr, persistent_worker_state);
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

} // namespace

DDRes main_loop(const WorkerAttr *attr, DDProfContext *ctx) {
  // Setup a shared memory region between the parent and child processes.  This
  // is used to communicate terminal profiling state
  int const mmap_prot = PROT_READ | PROT_WRITE;
  int const mmap_flags = MAP_ANONYMOUS | MAP_SHARED;
  auto *persistent_worker_state = static_cast<PersistentWorkerState *>(mmap(
      nullptr, sizeof(PersistentWorkerState), mmap_prot, mmap_flags, -1, 0));
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
    worker(*ctx, attr, persistent_worker_state);
    // Ensure worker does not return,
    // because we don't want to free resources (perf_event fds,...) that are
    // shared between processes. Only free the context.
    delete ctx;
    exit(0);
  }
  return {};
}

} // namespace ddprof
