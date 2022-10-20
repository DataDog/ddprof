// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "allocation_tracker.hpp"

#include "ddres.hpp"
#include "defer.hpp"
#include "ipc.hpp"
#include "perf.hpp"
#include "pevent_lib.hpp"
#include "ringbuffer_utils.hpp"
#include "savecontext.hpp"
#include "syscalls.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <linux/perf_event.h>
#include <sys/types.h>
#include <unistd.h>

namespace ddprof {

// AllocationEvent has a layout compatible with perf_event_sample
struct AllocationEvent {
  perf_event_header hdr;
  struct sample_id sample_id;
  uint64_t period;
  uint64_t abi;
  uint64_t regs[PERF_REGS_COUNT];
  uint64_t size;
  std::byte data[PERF_SAMPLE_STACK_SIZE];
  uint64_t dyn_size;
  uint64_t weight;
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

AllocationTracker *AllocationTracker::_instance;
thread_local TrackerThreadLocalState AllocationTracker::_tl_state;

AllocationTracker::AllocationTracker() : _gen(std::random_device{}()) {}

AllocationTracker *AllocationTracker::create_instance() {
  static AllocationTracker tracker;
  return &tracker;
}

DDRes AllocationTracker::allocation_tracking_init(
    uint64_t allocation_profiling_rate, uint32_t flags,
    const RingBufferInfo &ring_buffer) {
  ReentryGuard guard(&_tl_state.reentry_guard);

  AllocationTracker *instance = create_instance();
  auto &state = instance->_state;
  std::lock_guard lock{state.mutex};

  if (state.track_allocations) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_UKNW, "Allocation profiler already started");
  }

  // force initialization of malloc wrappers if not done yet
  // volatile prevents compiler from optimizing out calls to malloc/free
  void *volatile p = ::malloc(1);
  ::free(p);

  DDRES_CHECK_FWD(instance->init(allocation_profiling_rate,
                                 flags & kDeterministicSampling, ring_buffer));
  _instance = instance;
  state.track_allocations = true;
  state.track_deallocations = flags & kTrackDeallocations;

  return {};
}

DDRes AllocationTracker::init(uint64_t mem_profile_interval,
                              bool deterministic_sampling,
                              const RingBufferInfo &ring_buffer) {
  _sampling_interval = mem_profile_interval;
  _deterministic_sampling = deterministic_sampling;
  if (ring_buffer.ring_buffer_type !=
      static_cast<int>(RingBufferType::kMPSCRingBuffer)) {
    return ddres_error(DD_WHAT_PERFRB);
  }
  return ddprof::ring_buffer_attach(ring_buffer, &_pevent);
}

void AllocationTracker::free() {
  _state.track_allocations = false;
  _state.track_deallocations = false;

  pevent_munmap_event(&_pevent);

  // Do not destroy the object:
  // there is an inherent race condition between checking
  // `_state.track_allocation` and calling `_instance->track_allocation`.
  // That's why AllocationTracker is kept in a usable state and
  // `_track_allocation` is checked again in `_instance->track_allocation` while
  // taking the mutex lock.
  _instance = nullptr;
}

void AllocationTracker::allocation_tracking_free() {
  AllocationTracker *instance = _instance;
  if (!instance) {
    return;
  }
  ReentryGuard guard(&_tl_state.reentry_guard);
  std::lock_guard lock{instance->_state.mutex};
  instance->free();
}

void AllocationTracker::track_allocation(uintptr_t, size_t size,
                                         TrackerThreadLocalState &tl_state) {
  // Prevent reentrancy to avoid dead lock on mutex
  ReentryGuard guard(&tl_state.reentry_guard);

  if (!guard) {
    // Don't count internal allocations
    tl_state.remaining_bytes -= size;
    return;
  }

  // \fixme{nsavoire} reduce the scope of the mutex:
  // change prng/make it thread local
  std::lock_guard lock{_state.mutex};

  // recheck if profiling is enabled
  if (!_state.track_allocations) {
    return;
  }

  int64_t remaining_bytes = tl_state.remaining_bytes;

  if (unlikely(!tl_state.remaining_bytes_initialized)) {
    // tl_state.remaining bytes was not initialized yet for this thread
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

  if (_sampling_interval > 1) {
    // Determine the allocation count as the inverse probability of sampling an
    // allocation of size `size`.
    // For large allocations, nsamples ~= 1
    // For small allocations, nsamples ~= _sample_interval / size
    nsamples = static_cast<size_t>(
        1.0 /
        (1.0 - std::exp(-static_cast<double>(size) / _sampling_interval)));
  } else {
    nsamples = 1;
  }
  if (!IsDDResOK(push_sample(nsamples, total_size, tl_state))) {
    ++_state.failure_count;
    if (_state.failure_count >= k_max_consecutive_failures) {
      // Too many errors during ring buffer operation: stop allocation profiling
      free();
    }
  } else {
    if (_state.failure_count.load(std::memory_order_relaxed) > 0) {
      _state.failure_count = 0;
    }
  }
}

DDRes AllocationTracker::push_lost_sample(MPSCRingBufferWriter &writer,
                                          bool &notify_needed) {
  auto lost_count = _state.lost_count.exchange(0, std::memory_order_acq_rel);
  if (lost_count == 0) {
    return {};
  }
  bool timeout = false;
  auto buffer = writer.reserve(sizeof(LostEvent), &timeout);
  if (buffer.empty()) {
    // buffer is full, put back lost samples
    _state.lost_count.fetch_add(lost_count, std::memory_order_acq_rel);
    if (timeout) {
      return ddres_error(DD_WHAT_PERFRB);
    }
    return {};
  }

  LostEvent *lost_event = reinterpret_cast<LostEvent *>(buffer.data());
  lost_event->hdr.size = sizeof(LostEvent);
  lost_event->hdr.misc = 0;
  lost_event->hdr.type = PERF_RECORD_LOST;
  lost_event->id = 0;
  lost_event->lost = lost_count;
  notify_needed = writer.commit(buffer);

  return {};
}

DDRes AllocationTracker::push_sample(uint64_t alloc_count,
                                     uint64_t allocated_size,
                                     TrackerThreadLocalState &tl_state) {
  MPSCRingBufferWriter writer{_pevent.rb};
  bool notify_consumer{false};

  bool timeout = false;
  if (unlikely(_state.lost_count.load(std::memory_order_relaxed))) {
    DDRES_CHECK_FWD(push_lost_sample(writer, notify_consumer));
  }

  auto buffer = writer.reserve(sizeof(AllocationEvent), &timeout);

  if (buffer.empty()) {
    // ring buffer is full, increase lost count
    _state.lost_count.fetch_add(1, std::memory_order_acq_rel);

    if (timeout) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFRB,
                             "Unable to get write lock on ring buffer");
    }
    // not an error
    return {};
  }

  AllocationEvent *event = reinterpret_cast<AllocationEvent *>(buffer.data());
  event->hdr.misc = 0;
  event->hdr.size = sizeof(AllocationEvent);
  event->hdr.type = PERF_RECORD_SAMPLE;
  event->abi = PERF_SAMPLE_REGS_ABI_64;
  event->sample_id.time = 0;

  if (_state.pid == 0) {
    _state.pid = getpid();
  }
  if (tl_state.tid == 0) {
    tl_state.tid = ddprof::gettid();
  }

  if (tl_state.stack_end == nullptr) {
    // This call should only occur on main thread
    tl_state.stack_end = retrieve_stack_end_address();
  }

  event->sample_id.pid = _state.pid;
  event->sample_id.tid = tl_state.tid;
  event->period = allocated_size;
  event->weight = alloc_count;
  event->size = PERF_SAMPLE_STACK_SIZE;

  event->dyn_size = save_context(tl_state.stack_end, event->regs,
                                 ddprof::Buffer{event->data, event->size});

  if (writer.commit(buffer) || notify_consumer) {
    uint64_t count = 1;
    if (write(_pevent.fd, &count, sizeof(count)) != sizeof(count)) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFRB,
                             "Error writing to memory allocation eventfd (%s)",
                             strerror(errno));
    }
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
  std::uniform_real_distribution<> dist;
  double value = -std::log(dist(_gen)) * _sampling_interval;
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

void AllocationTracker::notify_thread_start() {
  TrackerThreadLocalState &tl_state = AllocationTracker::_tl_state;

  ReentryGuard guard(&_tl_state.reentry_guard);
  tl_state.stack_end = retrieve_stack_end_address();
}

void AllocationTracker::notify_fork() {
  if (_instance) {
    _instance->_state.pid = 0;
    _tl_state.tid = 0;
  }
}

} // namespace ddprof
