// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "allocation_tracker.hpp"

#include "allocation_event.hpp"
#include "ddprof_perf_event.hpp"
#include "ddres.hpp"
#include "ipc.hpp"
#include "lib_logger.hpp"
#include "live_allocation-c.hpp"
#include "pevent_lib.hpp"
#include "ringbuffer_utils.hpp"
#include "savecontext.hpp"
#include "syscalls.hpp"

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <unistd.h>

namespace ddprof {

struct LostEvent {
  perf_event_header hdr;
  uint64_t id;
  uint64_t lost;
};

// Static declarations
pthread_once_t AllocationTracker::_key_once = PTHREAD_ONCE_INIT;

pthread_key_t AllocationTracker::_tl_state_key;
ThreadEntries AllocationTracker::_thread_entries;

AllocationTracker *AllocationTracker::_instance;

TrackerThreadLocalState *AllocationTracker::get_tl_state() {
  // In shared libraries, TLS access requires a call to tls_get_addr,
  // tls_get_addr can call into malloc, which can create a recursive loop
  // instead we call pthread APIs to control the creation of TLS objects
  pthread_once(&_key_once, make_key);
  TrackerThreadLocalState *tl_state =
      (TrackerThreadLocalState *)pthread_getspecific(_tl_state_key);
  return tl_state;
}

TrackerThreadLocalState *AllocationTracker::init_tl_state() {
  TrackerThreadLocalState *tl_state = nullptr;
  int res_set = 0;

  pid_t tid = ddprof::gettid();
  // As we allocate within this function, this can be called twice
  TLReentryGuard tl_reentry_guard(_thread_entries, tid);
  if (!tl_reentry_guard) {
#ifdef DEBUG
    fprintf(stderr, "Unable to grab reentry guard %d \n", tid);
#endif
    return tl_state;
  }

  tl_state = new TrackerThreadLocalState();
  res_set = pthread_setspecific(_tl_state_key, tl_state);
  tl_state->tid = tid;

  if (res_set) {
    // should return 0
    log_once("Error: Unable to store tl_state. error %d \n", res_set);
    delete tl_state;
    tl_state = nullptr;
  }
  return tl_state;
}

AllocationTracker::AllocationTracker() {}

AllocationTracker *AllocationTracker::create_instance() {
  static AllocationTracker tracker;
  return &tracker;
}

void AllocationTracker::delete_tl_state(void *tl_state) {
  delete (TrackerThreadLocalState *)tl_state;
}

void AllocationTracker::make_key() {
  // delete is called on all key objects
  pthread_key_create(&_tl_state_key, delete_tl_state);
}

DDRes AllocationTracker::allocation_tracking_init(
    uint64_t allocation_profiling_rate, uint32_t flags,
    uint32_t stack_sample_size, const RingBufferInfo &ring_buffer) {
  TrackerThreadLocalState *tl_state = get_tl_state();
  if (!tl_state) {
    // This is the time at which the init_tl_state should not fail
    // We will not attempt to re-create it in other code paths
    tl_state = init_tl_state();
    if (!tl_state) {
      return ddres_error(DD_WHAT_DWFL_LIB_ERROR);
    }
  }

  ReentryGuard guard(&tl_state->reentry_guard);

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
                                 flags & kDeterministicSampling,
                                 stack_sample_size, ring_buffer));
  _instance = instance;

  state.init(true, flags & kTrackDeallocations);

  return {};
}

DDRes AllocationTracker::init(uint64_t mem_profile_interval,
                              bool deterministic_sampling,
                              uint32_t stack_sample_size,
                              const RingBufferInfo &ring_buffer) {
  _sampling_interval = mem_profile_interval;
  _deterministic_sampling = deterministic_sampling;
  _stack_sample_size = stack_sample_size;
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
  // `_state.track_allocations ` and calling `_instance->track_allocation`.
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
  if (instance->_state.track_deallocations) {
    instance->_dealloc_bitset.clear();
  }
  TrackerThreadLocalState *tl_state = get_tl_state();
  if (unlikely(!tl_state)) {
    log_once("Error: Unable to find tl_state during %s\n", __FUNCTION__);
    instance->free();
    return;
  }
  ReentryGuard guard(&tl_state->reentry_guard);
  std::lock_guard lock{instance->_state.mutex};
  instance->free();
}

void AllocationTracker::free_on_consecutive_failures(bool success) {
  if (!success) {
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

void AllocationTracker::track_allocation(uintptr_t addr, size_t size,
                                         TrackerThreadLocalState &tl_state) {
  // Prevent reentrancy to avoid dead lock on mutex
  ReentryGuard guard(&tl_state.reentry_guard);

  if (!guard) {
    // Don't count internal allocations
    tl_state.remaining_bytes -= size;
    return;
  }

  // recheck if profiling is enabled
  if (!_state.track_allocations) {
    return;
  }

  int64_t remaining_bytes = tl_state.remaining_bytes;

  if (unlikely(!tl_state.remaining_bytes_initialized)) {
    // tl_state.remaining bytes was not initialized yet for this thread
    remaining_bytes -= next_sample_interval(tl_state._gen);
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
    remaining_bytes -= next_sample_interval(tl_state._gen);
    ++nsamples;
  } while (remaining_bytes >= 0);

  tl_state.remaining_bytes = remaining_bytes;
  uint64_t total_size = nsamples * sampling_interval;

  if (_state.track_deallocations) {
    if (_dealloc_bitset.set(addr)) {
      if (unlikely(_dealloc_bitset.nb_elements() >
                   ddprof::liveallocation::kMaxTracked)) {
        // Check if we reached max number of elements
        // Clear elements if we reach too many
        // todo: should we just stop new live tracking (like java) ?
        if (IsDDResOK(push_clear_live_allocation(tl_state))) {
          _dealloc_bitset.clear();
          // still set this as we are pushing the allocation to ddprof
          _dealloc_bitset.set(addr);
        } else {
          log_once(
              "Error: %s",
              "Stop allocation profiling. Unable to clear live allocation \n");
          free();
        }
      }
    } else {
      // we won't track this allocation as we can't double count in the bitset
      return;
    }
  }
  bool success = IsDDResOK(push_alloc_sample(addr, total_size, tl_state));
  free_on_consecutive_failures(success);
  if (unlikely(!success) && _state.track_deallocations) {
    _dealloc_bitset.unset(addr);
  }
}

void AllocationTracker::track_deallocation(uintptr_t addr,
                                           TrackerThreadLocalState &tl_state) {
  // Prevent reentrancy to avoid dead lock on mutex
  ReentryGuard guard(&tl_state.reentry_guard);

  if (!guard) {
    // This is an internal dealloc, so we don't need to keep track of this
    return;
  }

  if (!_state.track_deallocations || !_dealloc_bitset.unset(addr)) {
    return;
  }

  bool success = IsDDResOK(push_dealloc_sample(addr, tl_state));
  free_on_consecutive_failures(success);
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

// Return true if consumer should be notified
DDRes AllocationTracker::push_clear_live_allocation(
    TrackerThreadLocalState &tl_state) {
  MPSCRingBufferWriter writer{_pevent.rb};
  bool timeout = false;

  auto buffer = writer.reserve(sizeof(ClearLiveAllocationEvent), &timeout);
  if (buffer.empty()) {
    // unable to push a clear is an error (we don't want to grow too much)
    // No use pushing a lost event. As this is a sync mechanism.
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFRB,
                           "Unable to get write lock on ring buffer");
  }

  ClearLiveAllocationEvent *event =
      reinterpret_cast<ClearLiveAllocationEvent *>(buffer.data());
  event->hdr.misc = 0;
  event->hdr.size = sizeof(ClearLiveAllocationEvent);
  event->hdr.type = PERF_CUSTOM_EVENT_CLEAR_LIVE_ALLOCATION;
  event->sample_id.time = 0;
  if (_state.pid == 0) {
    _state.pid = getpid();
  }
  if (tl_state.tid == 0) {
    tl_state.tid = ddprof::gettid();
  }
  event->sample_id.pid = _state.pid;
  event->sample_id.tid = tl_state.tid;

  if (writer.commit(buffer)) {
    uint64_t count = 1;
    if (write(_pevent.fd, &count, sizeof(count)) != sizeof(count)) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFRB,
                             "Error writing to memory allocation eventfd (%s)",
                             strerror(errno));
    }
  }

  return {};
}

DDRes AllocationTracker::push_dealloc_sample(
    uintptr_t addr, TrackerThreadLocalState &tl_state) {
  MPSCRingBufferWriter writer{_pevent.rb};
  bool notify_consumer{false};

  bool timeout = false;
  if (unlikely(_state.lost_count.load(std::memory_order_relaxed))) {
    DDRES_CHECK_FWD(push_lost_sample(writer, notify_consumer));
  }

  auto buffer = writer.reserve(sizeof(DeallocationEvent), &timeout);
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

  DeallocationEvent *event =
      reinterpret_cast<DeallocationEvent *>(buffer.data());
  event->hdr.misc = 0;
  event->hdr.size = sizeof(DeallocationEvent);
  event->hdr.type = PERF_CUSTOM_EVENT_DEALLOCATION;
  event->sample_id.time = 0;

  if (_state.pid == 0) {
    _state.pid = getpid();
  }
  if (tl_state.tid == 0) {
    tl_state.tid = ddprof::gettid();
  }
  event->sample_id.pid = _state.pid;
  event->sample_id.tid = tl_state.tid;

  // address of dealloc
  event->ptr = addr;

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

DDRes AllocationTracker::push_alloc_sample(uintptr_t addr,
                                           uint64_t allocated_size,
                                           TrackerThreadLocalState &tl_state) {
  MPSCRingBufferWriter writer{_pevent.rb};
  bool notify_consumer{false};

  bool timeout = false;
  if (unlikely(_state.lost_count.load(std::memory_order_relaxed))) {
    DDRES_CHECK_FWD(push_lost_sample(writer, notify_consumer));
  }

  auto buffer =
      writer.reserve(sizeof_allocation_event(_stack_sample_size), &timeout);

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
  event->hdr.size = sizeof_allocation_event(_stack_sample_size);
  event->hdr.type = PERF_RECORD_SAMPLE;
  event->abi = PERF_SAMPLE_REGS_ABI_64;
  event->sample_id.time = 0;
  event->addr = addr;
  if (_state.pid == 0) {
    _state.pid = getpid();
  }
  if (tl_state.tid == 0) {
    tl_state.tid = ddprof::gettid();
  }

  if (tl_state.stack_bounds.empty()) {
    // This call should only occur on main thread
    tl_state.stack_bounds = retrieve_stack_bounds();
    if (tl_state.stack_bounds.empty()) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFRB, "Unable to get thread bounds");
    }
  }

  event->sample_id.pid = _state.pid;
  event->sample_id.tid = tl_state.tid;
  event->period = allocated_size;
  event->size_stack = _stack_sample_size;

  std::byte *dyn_size_pos = event->data + _stack_sample_size;
  uint64_t *dyn_size = reinterpret_cast<uint64_t *>(dyn_size_pos);

  assert(reinterpret_cast<uintptr_t>(dyn_size) % alignof(uint64_t) == 0);

  (*dyn_size) = save_context(tl_state.stack_bounds, event->regs,
                             ddprof::Buffer{event->data, event->size_stack});
  // Even if dyn_size == 0, we keep the sample
  // This way, the overall accounting is correct (even with empty stacks)
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

uint64_t AllocationTracker::next_sample_interval(std::minstd_rand &gen) {
  if (_sampling_interval == 1) {
    return 1;
  }
  if (_deterministic_sampling) {
    return _sampling_interval;
  }
  double sampling_rate = 1.0 / static_cast<double>(_sampling_interval);
  std::exponential_distribution<> dist(sampling_rate);
  double value = dist(gen);
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
  TrackerThreadLocalState *tl_state = get_tl_state();
  if (unlikely(!tl_state)) {
    tl_state = init_tl_state();
    if (!tl_state) {
      log_once("Error: Unable to start allocation profiling on thread %d",
               ddprof::gettid());
      return;
    }
  }

  ReentryGuard guard(&tl_state->reentry_guard);
  tl_state->stack_bounds = retrieve_stack_bounds();
  // error can not be propagated in thread create
}

void AllocationTracker::notify_fork() {
  _thread_entries.reset();
  if (_instance) {
    _instance->_state.pid = 0;
  }
  TrackerThreadLocalState *tl_state = get_tl_state();
  if (unlikely(!tl_state)) {
    // The state should already exist if we forked.
    // This would mean that we were not able to create the state before forking
    log_once("Error: Unable to retrieve tl state after fork thread %d",
             ddprof::gettid());
    return;
  } else {
    tl_state->tid = 0;
  }
}

} // namespace ddprof
