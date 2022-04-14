// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "allocation_tracker.hpp"

extern "C" {
#include "perf.h"
#include "pevent_lib.h"
}

#include "ddres.h"
#include "defer.hpp"
#include "ipc.hpp"
#include "ringbuffer_utils.hpp"
#include "savecontext.hpp"
#include "syscalls.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <linux/perf_event.h>
#include <sys/types.h>
#include <unistd.h>

namespace ddprof {

struct AllocationEvent {
  perf_event_header hdr;
  struct sample_id sample_id;
  uint64_t period;
  uint64_t abi; /* if PERF_SAMPLE_REGS_USER */
  uint64_t regs[PERF_REGS_COUNT];
  /* if PERF_SAMPLE_REGS_USER */
  uint64_t size;                          /* if PERF_SAMPLE_STACK_USER */
  std::byte data[PERF_SAMPLE_STACK_SIZE]; /* if PERF_SAMPLE_STACK_USER */
  uint64_t dyn_size;                      /* if PERF_SAMPLE_STACK_USER &&
                                        size != 0 */
};

struct LostEvent {
  perf_event_header hdr;
  uint64_t id;
  uint64_t lost;
};

class ReentryGuard {
public:
  explicit ReentryGuard(bool *reentry_guard)
      : _reentry_guard(reentry_guard), _ok(!*reentry_guard) {
    *_reentry_guard = true;
  }
  ~ReentryGuard() {
    if (_ok) {
      *_reentry_guard = false;
    }
  }

  explicit operator bool() const { return _ok; }

  ReentryGuard(const ReentryGuard &) = delete;
  ReentryGuard &operator=(const ReentryGuard &) = delete;

private:
  bool *_reentry_guard;
  bool _ok;
};

TrackerStaticState AllocationTrackerState::state;
thread_local TrackerThreadLocalState AllocationTrackerState::tl_state;

static void allocation_tracking_free_locked();

AllocationTracker::AllocationTracker() : _gen(std::random_device{}()) {}

AllocationTracker *AllocationTracker::get_instance() {
  static AllocationTracker tracker;
  return &tracker;
}

DDRes AllocationTracker::init(uint64_t mem_profile_interval,
                              bool deterministic_sampling,
                              const RingBufferInfo &ring_buffer) {
  _sampling_interval = mem_profile_interval;
  _deterministic_sampling = deterministic_sampling;
  pevent_init(&_pevent_hdr);
  _pevent_hdr.size = 1;
  PEvent &pe = _pevent_hdr.pes[0];
  pe.fd = ring_buffer.event_fd;
  pe.mapfd = ring_buffer.ring_fd;
  pe.pos = -1;
  pe.custom_event = true;
  return pevent_mmap(&_pevent_hdr, true);
}

void AllocationTracker::free() { pevent_munmap(&_pevent_hdr); }

void AllocationTracker::track_allocation(uintptr_t, size_t size,
                                         TrackerThreadLocalState &tl_state) {
  // Prevent reentrancy to avoid dead lock on mutex
  ReentryGuard guard(&tl_state.reentry_guard);

  if (!guard) {
    // Don't count internal allocations
    tl_state.remaining_bytes -= size;
    return;
  }

  TrackerStaticState &state = AllocationTrackerState::state;

  // \fixme reduce the scope of the mutex: change prng/make it thread local
  std::lock_guard lock{state.mutex};

  // recheck if profiling is enabled
  if (!state.track_allocations) {
    return;
  }

  int64_t remaining_bytes = tl_state.remaining_bytes;

  if (unlikely(!tl_state.remaining_bytes_initialized)) {
    // s_remaining bytes was not initialized yet for this thread
    remaining_bytes -= next_sample_interval();
    tl_state.remaining_bytes_initialized = true;
    if (remaining_bytes < 0) {
      tl_state.remaining_bytes = remaining_bytes;
      return;
    }
  }

  // compute number of samples this allocation should be accounted for
  auto sampling_interval = _sampling_interval;
  size_t nsamples = remaining_bytes / sampling_interval;
  remaining_bytes = remaining_bytes % sampling_interval;

  do {
    remaining_bytes -= next_sample_interval();
    ++nsamples;
  } while (remaining_bytes >= 0);

  tl_state.remaining_bytes = remaining_bytes;
  uint64_t total_size = nsamples * sampling_interval;

  if (!IsDDResOK(push_sample(total_size, tl_state))) {
    // error during ring buffer operation: stop allocation profiling
    allocation_tracking_free_locked();
  }
}

DDRes AllocationTracker::push_sample(uint64_t allocated_size,
                                     TrackerThreadLocalState &tl_state) {
  {
    RingBufferWriter writer{_pevent_hdr.pes[0].rb};
    auto needed_size = sizeof(AllocationEvent);

    TrackerStaticState &state = AllocationTrackerState::state;
    if (state.lost_count) {
      needed_size += sizeof(LostEvent);
    }

    if (writer.available_size() < needed_size) {
      // ring buffer is full, increase lost count
      ++state.lost_count;

      // not an error
      return {};
    }

    Buffer buf = writer.reserve(needed_size);
    AllocationEvent *event = reinterpret_cast<AllocationEvent *>(buf.data());
    event->hdr.misc = 0;
    event->hdr.size = sizeof(AllocationEvent);
    event->hdr.type = PERF_RECORD_SAMPLE;
    event->abi = PERF_SAMPLE_REGS_ABI_64;
    event->sample_id.time = 0;

    if (state.pid == 0) {
      // \fixme reset on fork
      state.pid = getpid();
    }
    if (tl_state.tid == 0) {
      tl_state.tid = ddprof::gettid();
    }

    event->sample_id.pid = state.pid;
    event->sample_id.tid = tl_state.tid;
    event->period = allocated_size;
    event->size = PERF_SAMPLE_STACK_SIZE;

    event->dyn_size =
        save_context(event->regs, ddprof::Buffer{event->data, event->size});

    writer.write(
        ddprof::Buffer{reinterpret_cast<std::byte *>(event), sizeof(*event)});

    if (state.lost_count) {
      LostEvent lost_event;
      lost_event.hdr.size = sizeof(LostEvent);
      lost_event.hdr.misc = 0;
      lost_event.hdr.type = PERF_RECORD_LOST;
      lost_event.id = 0;
      lost_event.lost = state.lost_count;
      state.lost_count = 0;

      writer.write(ddprof::Buffer{reinterpret_cast<std::byte *>(&lost_event),
                                  sizeof(lost_event)});
    }
  }
  uint64_t count = 1;
  if (write(_pevent_hdr.pes[0].fd, &count, sizeof(count)) != sizeof(count)) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFRB,
                           "Error writing to memory allocation eventfd (%s)",
                           strerror(errno));
  }

  return {};
}

uint64_t AllocationTracker::next_sample_interval() {
  if (_sampling_interval == 1) {
    return 1;
  }
  if (_deterministic_sampling) {
    return _sampling_interval;
  }
  double sampling_rate = 1.0 / static_cast<double>(_sampling_interval);
  std::exponential_distribution<> dist(sampling_rate);
  double value = dist(_gen);
  const size_t max_value = _sampling_interval * 20;
  const size_t min_value = 8;
  if (value > max_value) {
    value = max_value;
  }
  if (value < min_value) {
    value = min_value;
  }
  return value;
}

DDRes allocation_tracking_init(uint64_t allocation_profiling_rate,
                               uint32_t flags,
                               const RingBufferInfo &ring_buffer) {
  auto &state = AllocationTrackerState::state;
  std::lock_guard lock{state.mutex};

  if (state.track_allocations) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_UKNW, "Allocation profiler already started");
  }
  state.instance = AllocationTracker::get_instance();

  DDRES_CHECK_FWD(state.instance->init(
      allocation_profiling_rate,
      flags &
          static_cast<uint32_t>(
              AllocationTrackingFlags::kDeterministicSampling),
      ring_buffer));

  state.track_allocations = true;
  state.track_deallocations = flags &
      static_cast<uint32_t>(AllocationTrackingFlags::kTrackDeallocations);

  return {};
}

static void allocation_tracking_free_locked() {
  auto &state = AllocationTrackerState::state;

  state.track_allocations = false;
  state.track_deallocations = false;

  // Do not set `s_instance` to NULL, nor destroy the object:
  // there is an inherent race condition between checking `s_track_allocation`
  // and calling `s_instance->track_allocation`.
  // That's why `s_instance` is kept in a usable state and `s_track_allocation`
  // is checked again in `s_instance->track_allocation` while taking the mutex
  // lock.
  state.instance->free();
}

void allocation_tracking_free() {
  std::lock_guard lock{AllocationTrackerState::state.mutex};
  allocation_tracking_free_locked();
}

} // namespace ddprof
