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

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ddprof {
namespace {

pid_t g_child_pid = 0;
bool g_termination_requested = false;

void handle_signal(int /*unused*/) {
  g_termination_requested = true;

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
      waitpid(g_child_pid, nullptr, 0);
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

void pollfd_setup(const PEventHdr *pevent_hdr, struct pollfd *pfd,
                  int *pfd_len) {
  *pfd_len = pevent_hdr->size;
  const PEvent *pes = pevent_hdr->pes;
  // Setup poll() to watch perf_event file descriptors
  for (int i = 0; i < *pfd_len; ++i) {
    // NOTE: if fd is negative, it will be ignored
    pfd[i].fd = pes[i].fd;
    pfd[i].events = POLLIN;
  }
}

DDRes signalfd_setup(pollfd *pfd) {
  sigset_t mask;

  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);

  // no need to block signal, since we inherited sigprocmask from parent
  int const sfd = signalfd(-1, &mask, 0);
  DDRES_CHECK_ERRNO(sfd, DD_WHAT_WORKERLOOP_INIT, "Could not set signalfd");

  pfd->fd = sfd;
  pfd->events = POLLIN;
  return {};
}

inline DDRes
worker_process_ring_buffers(PEvent *pes, int pe_len, DDProfContext &ctx,
                            std::chrono::steady_clock::time_point *now,
                            const int *ring_buffer_start_idx) {
  // While there are events to process, iterate through them
  // while limiting time spent in loop to at most k_sample_default_wakeup
  auto loop_start = std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point local_now;

  int start_idx = *ring_buffer_start_idx;
  bool events;
  do {
    events = false;
    for (int i = start_idx; i < pe_len; ++i) {
      auto &ring_buffer = pes[i].rb;
      if (ring_buffer.type == RingBufferType::kPerfRingBuffer) {
        PerfRingBufferReader reader(&ring_buffer);

        ConstBuffer buffer = reader.read_all_available();
        while (!buffer.empty()) {
          const auto *hdr =
              reinterpret_cast<const perf_event_header *>(buffer.data());
          DDRes res = ddprof_worker_process_event(hdr, pes[i].watcher_pos, ctx);

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
          DDRes res = ddprof_worker_process_event(hdr, pes[i].watcher_pos, ctx);

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
    start_idx = 0;
    local_now = std::chrono::steady_clock::now();
  } while (events && (local_now - loop_start) < k_sample_default_wakeup);

  *now = local_now;
  return {};
}

DDRes worker_loop(DDProfContext &ctx, const WorkerAttr *attr,
                  PersistentWorkerState *persistent_worker_state) {

  // Setup poll() to watch perf_event file descriptors
  int const pe_len = ctx.worker_ctx.pevent_hdr.size;
  // one extra slot in pfd to accommodate for signal fd
  pollfd poll_fds[k_max_nb_perf_event_open + 1];
  int pfd_len = 0;
  pollfd_setup(&ctx.worker_ctx.pevent_hdr, poll_fds, &pfd_len);

  DDRES_CHECK_FWD(signalfd_setup(&poll_fds[pfd_len]));
  int const signal_pos = pfd_len++;

  WorkerServer const server =
      start_worker_server(ctx.socket_fd.get(), create_reply_message(ctx));

  // Perform user-provided initialization
  defer { attr->finish_fun(ctx); };
  DDRES_CHECK_FWD(attr->init_fun(ctx, persistent_worker_state));

  // ring_buffer_start_idx serves to indicate at which ring buffer index
  // processing loop should restart if it was interrupted in the middle of the
  // loop by timeout.
  // Not used yet, because currently we process all ring buffers or none
  int const ring_buffer_start_idx = 0;
  bool stop = false;

  // Worker poll loop
  while (!stop) {
    // Convenience structs
    PEvent *pes = ctx.worker_ctx.pevent_hdr.pes;

    int const n =
        poll(poll_fds, pfd_len,
             std::chrono::milliseconds{k_sample_default_wakeup}.count());

    // If there was an issue, return and let the caller check errno
    if (-1 == n && errno == EINTR) {
      continue;
    }
    DDRES_CHECK_ERRNO(n, DD_WHAT_POLLERROR, "poll failed");

    if (poll_fds[signal_pos].revents & POLLIN) {
      LG_NFO("Received termination signal");
      break;
    }

    for (int i = 0; i < pe_len; ++i) {
      pollfd const &pfd = poll_fds[i];
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

    std::chrono::steady_clock::time_point now;
    DDRES_CHECK_FWD(worker_process_ring_buffers(pes, pe_len, ctx, &now,
                                                &ring_buffer_start_idx));
    DDRES_CHECK_FWD(ddprof_worker_maybe_export(ctx, now));

    if (ctx.worker_ctx.persistent_worker_state->restart_worker) {
      // return directly no need to do a final export
      return {};
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
