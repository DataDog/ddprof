// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "allocation_tracker.hpp"

#include "allocation_event.hpp"
#include "ddprof_perf_event.hpp"
#include "ddres_def.hpp"
#include "ipc.hpp"
#include "perf_clock.hpp"
#include "pevent_lib.hpp"
#include "reentry_guard.hpp"
#include "ringbuffer_utils.hpp"
#include "savecontext.hpp"
#include "syscalls.hpp"
#include "tsc_clock.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <unistd.h>

namespace ddprof {

// Static declarations
pthread_once_t AllocationTracker::_key_once = PTHREAD_ONCE_INIT;

pthread_key_t AllocationTracker::_tl_state_key;

AllocationTracker *AllocationTracker::_instance;

namespace {
DDPROF_NOINLINE auto sleep_and_retry_reserve(MPSCRingBufferWriter &writer,
                                             size_t size, bool &timeout) {
  constexpr std::chrono::nanoseconds k_sleep_duration =
      std::chrono::microseconds(500);
  constexpr int k_max_sleep_attempts = 5;

  for (int i = 0; i < k_max_sleep_attempts; i++) {
    std::this_thread::sleep_for(k_sleep_duration);
    auto buffer = writer.reserve(size, &timeout, true);
    if (!buffer.empty() || timeout) {
      return buffer;
    }
  }
  return Buffer{};
}
} // namespace

TrackerThreadLocalState *AllocationTracker::get_tl_state() {
  // In shared libraries, TLS access requires a call to tls_get_addr,
  // tls_get_addr can call into malloc, which can create a recursive loop
  // instead we call pthread APIs to control the creation of TLS objects
  pthread_once(&_key_once, make_key);
  auto *tl_state = static_cast<TrackerThreadLocalState *>(
      pthread_getspecific(_tl_state_key));
  return tl_state;
}

TrackerThreadLocalState *AllocationTracker::init_tl_state() {
  // Since init_tl_state is only called in allocation_tracking_init and
  // notify_thread_start, there is no danger of reentering it when doing an
  // allocation.
  auto tl_state = std::make_unique<TrackerThreadLocalState>();
  tl_state->tid = ddprof::gettid();
  tl_state->stack_bounds = retrieve_stack_bounds();

  if (int const res = pthread_setspecific(_tl_state_key, tl_state.get());
      res != 0) {
    // should return 0
    LG_DBG("Unable to store tl_state. Error %d: %s\n", res, strerror(res));
    tl_state.reset();
  }

  return tl_state.release();
}

AllocationTracker::AllocationTracker() = default;

AllocationTracker *AllocationTracker::create_instance() {
  static AllocationTracker tracker;
  return &tracker;
}

void AllocationTracker::delete_tl_state(void *tl_state) {
  delete static_cast<TrackerThreadLocalState *>(tl_state);
}

void AllocationTracker::make_key() {
  // delete is called on all key objects
  pthread_key_create(&_tl_state_key, delete_tl_state);
}

DDRes AllocationTracker::allocation_tracking_init(
    uint64_t allocation_profiling_rate, uint32_t flags,
    uint32_t stack_sample_size, const RingBufferInfo &ring_buffer,
    const IntervalTimerCheck &timer_check) {
  TrackerThreadLocalState *tl_state = get_tl_state();
  if (!tl_state) {
    // This is the time at which the init_tl_state should not fail
    // We will not attempt to re-create it in other code paths
    tl_state = init_tl_state();
    if (!tl_state) {
      return ddres_error(DD_WHAT_DWFL_LIB_ERROR);
    }
  }

  ReentryGuard const guard(&tl_state->reentry_guard);

  AllocationTracker *instance = create_instance();
  auto &state = instance->_state;
  std::lock_guard const lock{state.mutex};

  if (state.track_allocations) {
    // the log here is acceptable as we assume we are not in a reentrant state
    DDRES_RETURN_ERROR_LOG(DD_WHAT_UKNW, "Allocation profiler already started");
  }

  DDRES_CHECK_FWD(instance->init(allocation_profiling_rate,
                                 flags & kDeterministicSampling,
                                 flags & kTrackDeallocations, stack_sample_size,
                                 ring_buffer, timer_check));
  _instance = instance;

  state.init(true, flags & kTrackDeallocations);

  return {};
}

DDRes AllocationTracker::init(uint64_t mem_profile_interval,
                              bool deterministic_sampling,
                              bool track_deallocations,
                              uint32_t stack_sample_size,
                              const RingBufferInfo &ring_buffer,
                              const IntervalTimerCheck &timer_check) {
  _sampling_interval = mem_profile_interval;
  _deterministic_sampling = deterministic_sampling;
  _stack_sample_size = stack_sample_size;
  if (ring_buffer.ring_buffer_type !=
      static_cast<int>(RingBufferType::kMPSCRingBuffer)) {
    return ddres_error(DD_WHAT_PERFRB);
  }
  if (track_deallocations) {
    _allocated_address_set.init(0); // Use default size, fail on add if full
    constexpr double k_max_high_priority_area_size_fraction = 0.1;
    constexpr int64_t k_high_priority_event_count = 10000;
    _high_priority_area_size =
        std::min(static_cast<size_t>(ring_buffer.mem_size *
                                     k_max_high_priority_area_size_fraction),
                 k_high_priority_event_count * (sizeof(DeallocationEvent) + 8));
  } else {
    _high_priority_area_size = 0;
  }

  DDRES_CHECK_FWD(ddprof::ring_buffer_attach(ring_buffer, &_pevent));

  const auto &rb = _pevent.rb;
  if (rb.tsc_available) {
    TscClock::init(TscClock::CalibrationParams{
        .offset = TscClock::time_point{TscClock::duration{rb.time_zero}},
        .mult = rb.time_mult,
        .shift = rb.time_shift});
  }
  PerfClock::init(static_cast<PerfClockSource>(rb.perf_clock_source));

  _interval_timer_check = timer_check;
  if (_interval_timer_check.is_set()) {
    _state.next_check_time.store(
        _interval_timer_check.initial_delay.count()
            ? PerfClock::now() + _interval_timer_check.initial_delay
            : PerfClock::now() + _interval_timer_check.interval,
        std::memory_order_release);
  } else {
    _state.next_check_time.store(PerfClock::time_point::max(),
                                 std::memory_order_release);
  }

  return {};
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
  TrackerThreadLocalState *tl_state = get_tl_state();
  if (unlikely(!tl_state)) {
    LG_DBG("Unable to get tl_state during allocation_tracking_free");
    instance->free();
    return;
  }
  ReentryGuard const guard(&tl_state->reentry_guard);
  std::lock_guard const lock{instance->_state.mutex};
  instance->free();
}

void AllocationTracker::free_on_consecutive_failures(bool failure) {
  if (failure) {
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

void AllocationTracker::track_allocation(uintptr_t addr, size_t /*size*/,
                                         TrackerThreadLocalState &tl_state,
                                         bool is_large_alloc) {
  // Reentrancy should be prevented by caller (by using ReentryGuard on
  // TrackerThreadLocalState::reentry_guard).

  // recheck if profiling is enabled
  if (!_state.track_allocations) {
    return;
  }

  int64_t remaining_bytes = tl_state.remaining_bytes;

  if (unlikely(!tl_state.remaining_bytes_initialized)) {
    // tl_state.remaining bytes was not initialized yet for this thread
    remaining_bytes -= next_sample_interval(tl_state.gen);
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
    remaining_bytes -= next_sample_interval(tl_state.gen);
    ++nsamples;
  } while (remaining_bytes >= 0);

  tl_state.remaining_bytes = remaining_bytes;
  uint64_t const total_size = nsamples * sampling_interval;

  if (_state.track_deallocations) {
    if (!_allocated_address_set.add(addr, is_large_alloc)) {
      // add() returned false: either table is full or address already exists
      _state.address_conflict_count.fetch_add(1, std::memory_order_acq_rel);
      // null the address to avoid using this for live heap profiling
      // pushing a sample is still good to have a good representation
      // of the allocations.
      addr = 0;
    }
  }
  auto res = push_alloc_sample(addr, total_size, tl_state);
  free_on_consecutive_failures(IsDDResFatal(res));
  if (unlikely(!IsDDResOK(res)) && _state.track_deallocations && addr) {
    _allocated_address_set.remove(addr, is_large_alloc);
  }
}

void AllocationTracker::track_deallocation(uintptr_t addr,
                                           TrackerThreadLocalState &tl_state,
                                           bool is_large_alloc) {
  // Reentrancy should be prevented by caller (by using ReentryGuard on
  // TrackerThreadLocalState::reentry_guard).

  if (!_state.track_deallocations ||
      !_allocated_address_set.remove(addr, is_large_alloc)) {
    return;
  }

  auto fatal_failure = IsDDResFatal(push_dealloc_sample(addr, tl_state));
  free_on_consecutive_failures(fatal_failure);
}

DDRes AllocationTracker::push_allocation_tracker_state() {
  MPSCRingBufferWriter writer{&_pevent.rb, _high_priority_area_size};

  bool timeout = false;
  auto buffer =
      writer.reserve(sizeof(AllocationTrackerStateEvent), &timeout, true);
  if (buffer.empty()) {
    if (timeout) {
      return ddres_error(DD_WHAT_PERFRB);
    }
    return ddres_warn(DD_WHAT_PERFRB);
  }

  auto *event = reinterpret_cast<AllocationTrackerStateEvent *>(buffer.data());
  event->header.size = sizeof(AllocationTrackerStateEvent);
  event->header.misc = 0;
  event->header.type = PERF_CUSTOM_EVENT_ALLOCATION_TRACKER_STATE;
  auto now = PerfClock::now();
  event->sample_id.time = now.time_since_epoch().count();

  DDPROF_DCHECK_FATAL(_state.pid != 0, "pid is not set");
  event->sample_id.pid = _state.pid;
  event->sample_id.tid = 0;

  event->address_conflict_count =
      _state.address_conflict_count.exchange(0, std::memory_order_acq_rel);
  event->tracked_address_count = _allocated_address_set.count();
  event->active_shards = _allocated_address_set.active_shards();
  event->lost_alloc_count =
      _state.lost_alloc_count.exchange(0, std::memory_order_acq_rel);
  event->lost_dealloc_count =
      _state.lost_dealloc_count.exchange(0, std::memory_order_acq_rel);

  LG_DBG("Tracked address count: %d, address conflict count: %d, lost alloc "
         "count: %d, lost dealloc count: %d",
         event->tracked_address_count, event->address_conflict_count,
         event->lost_alloc_count, event->lost_dealloc_count);

  if (writer.commit(buffer)) {
    uint64_t count = 1;
    if (write(_pevent.fd, &count, sizeof(count)) != sizeof(count)) {
      LG_DBG("Error writing to memory allocation eventfd (%s)",
             strerror(errno));
      return DDRes{._what = DD_WHAT_PERFRB, ._sev = DD_SEV_ERROR};
    }
  }

  return {};
}

// Return true if consumer should be notified
DDRes AllocationTracker::push_clear_live_allocation(
    TrackerThreadLocalState &tl_state) {
  MPSCRingBufferWriter writer{&_pevent.rb, _high_priority_area_size};
  bool timeout = false;

  auto buffer =
      writer.reserve(sizeof(ClearLiveAllocationEvent), &timeout, true);
  if (buffer.empty()) {
    // unable to push a clear is an error (we don't want to grow too much)
    // No use pushing a lost event. As this is a sync mechanism.
    LG_DBG("Unable to get write lock on ring buffer");
    return DDRes{._what = DD_WHAT_PERFRB, ._sev = DD_SEV_ERROR};
  }

  auto *event = reinterpret_cast<ClearLiveAllocationEvent *>(buffer.data());
  event->hdr.misc = 0;
  event->hdr.size = sizeof(ClearLiveAllocationEvent);
  event->hdr.type = PERF_CUSTOM_EVENT_CLEAR_LIVE_ALLOCATION;
  auto now = PerfClock::now();
  event->sample_id.time = now.time_since_epoch().count();

  DDPROF_DCHECK_FATAL(_state.pid != 0 && tl_state.tid != 0,
                      "pid or tid is not set");
  event->sample_id.pid = _state.pid;
  event->sample_id.tid = tl_state.tid;

  if (writer.commit(buffer)) {
    uint64_t count = 1;
    if (write(_pevent.fd, &count, sizeof(count)) != sizeof(count)) {
      LG_DBG("Error writing to memory allocation eventfd (%s)",
             strerror(errno));
      return DDRes{._what = DD_WHAT_PERFRB, ._sev = DD_SEV_ERROR};
    }
  }

  check_timer(now, tl_state);

  return {};
}

DDRes AllocationTracker::push_dealloc_sample(
    uintptr_t addr, TrackerThreadLocalState &tl_state) {
  MPSCRingBufferWriter writer{&_pevent.rb, _high_priority_area_size};

  bool timeout = false;
  auto buffer = writer.reserve(sizeof(DeallocationEvent), &timeout, true);
  if (buffer.empty()) {
    buffer =
        sleep_and_retry_reserve(writer, sizeof(DeallocationEvent), timeout);
  }
  if (buffer.empty()) {
    // ring buffer is full, increase lost count
    _state.lost_dealloc_count.fetch_add(1, std::memory_order_acq_rel);
    if (timeout) {
      LG_DBG("Unable to get write lock on ring buffer");
      return DDRes{._what = DD_WHAT_PERFRB, ._sev = DD_SEV_ERROR};
    }
    // not an error
    return ddres_warn(DD_WHAT_PERFRB);
  }

  auto *event = reinterpret_cast<DeallocationEvent *>(buffer.data());
  event->hdr.misc = 0;
  event->hdr.size = sizeof(DeallocationEvent);
  event->hdr.type = PERF_CUSTOM_EVENT_DEALLOCATION;
  auto now = PerfClock::now();
  event->sample_id.time = now.time_since_epoch().count();

  DDPROF_DCHECK_FATAL(_state.pid != 0 && tl_state.tid != 0,
                      "pid or tid is not set");
  event->sample_id.pid = _state.pid;
  event->sample_id.tid = tl_state.tid;

  // address of dealloc
  event->ptr = addr;

  if (writer.commit(buffer)) {
    uint64_t count = 1;
    if (write(_pevent.fd, &count, sizeof(count)) != sizeof(count)) {
      LG_DBG("Error writing to memory allocation eventfd (%s)",
             strerror(errno));
      return DDRes{._what = DD_WHAT_PERFRB, ._sev = DD_SEV_ERROR};
    }
  }

  check_timer(now, tl_state);

  return {};
}

DDRes AllocationTracker::push_alloc_sample(uintptr_t addr,
                                           uint64_t allocated_size,
                                           TrackerThreadLocalState &tl_state) {
  MPSCRingBufferWriter writer{&_pevent.rb, _high_priority_area_size};
  // estimate sample stack size
  void *p;
  const auto *stack_base_ptr = reinterpret_cast<const std::byte *>(&p);
  auto stack_size = to_address(tl_state.stack_bounds.end()) - stack_base_ptr;

  // stack will be saved in save_context, add some margin to account for call
  // frames
#ifdef NDEBUG
  constexpr int64_t kStackMargin = 192;
#else
  constexpr int64_t kStackMargin = 720;
#endif
  uint32_t const sample_stack_size =
      align_up(std::min(std::max(stack_size + kStackMargin, 0L),
                        static_cast<int64_t>(_stack_sample_size)),
               sizeof(uint64_t));

  auto event_size = sizeof_allocation_event(sample_stack_size);

  bool timeout = false;
  auto buffer = writer.reserve(event_size, &timeout);

  if (buffer.empty()) {
    // ring buffer is full, increase lost count
    _state.lost_alloc_count.fetch_add(1, std::memory_order_acq_rel);

    if (timeout) {
      // The log here could deadlock (hence we put it behind a debug flag)
      LG_DBG("Unable to get write lock on ring buffer");
      return DDRes{._what = DD_WHAT_PERFRB, ._sev = DD_SEV_ERROR};
    }

    // not an error
    return ddres_warn(DD_WHAT_PERFRB);
  }

  auto *event = reinterpret_cast<AllocationEvent *>(buffer.data());
  std::byte *dyn_size_pos = event->data + sample_stack_size;
  auto *dyn_size = reinterpret_cast<uint64_t *>(dyn_size_pos);

  assert(reinterpret_cast<uintptr_t>(dyn_size) % alignof(uint64_t) == 0);

  (*dyn_size) = save_context(tl_state.stack_bounds, event->regs,
                             ddprof::Buffer{event->data, sample_stack_size});

  event->hdr.misc = 0;
  event->hdr.size = event_size;
  event->hdr.type = PERF_RECORD_SAMPLE;
  event->abi = PERF_SAMPLE_REGS_ABI_64;
  auto now = PerfClock::now();
  event->sample_id.time = now.time_since_epoch().count();
  event->addr = addr;

  DDPROF_DCHECK_FATAL(_state.pid != 0 && tl_state.tid != 0,
                      "pid or tid is not set");
  event->sample_id.pid = _state.pid;
  event->sample_id.tid = tl_state.tid;
  event->period = allocated_size;
  event->size_stack = sample_stack_size;

  // Even if dyn_size == 0, we keep the sample
  // This way, the overall accounting is correct (even with empty stacks)
  if (writer.commit(buffer)) {
    uint64_t count = 1;
    if (write(_pevent.fd, &count, sizeof(count)) != sizeof(count)) {
      // Logs can cause deadlock (hence we print it in debug mode only)
      LG_DBG("Error writing to memory allocation eventfd (%s)",
             strerror(errno));
      return DDRes{._what = DD_WHAT_PERFRB, ._sev = DD_SEV_ERROR};
    }
  }

  check_timer(now, tl_state);

  return {};
}

void AllocationTracker::check_timer(PerfClock::time_point now,
                                    TrackerThreadLocalState &tl_state) {
  if (tl_state.allocation_allowed &&
      now > _state.next_check_time.load(std::memory_order_acquire)) {
    update_timer(now);
  }
}

void AllocationTracker::update_timer(PerfClock::time_point now) {
  std::lock_guard const lock{_state.mutex};

  // recheck that we are the thread that should update the timer
  if (now <= _state.next_check_time.load()) {
    return;
  }

  if (!_interval_timer_check.is_set() ||
      _interval_timer_check.interval.count() == 0) {
    _state.next_check_time.store(PerfClock::time_point::max(),
                                 std::memory_order_release);
    return;
  }

  _state.next_check_time.store(now + _interval_timer_check.interval,
                               std::memory_order_release);
  push_allocation_tracker_state();
  _interval_timer_check.callback();
}

DDPROF_NOINLINE uint64_t
AllocationTracker::next_sample_interval(std::minstd_rand &gen) const {
  if (_sampling_interval == 1) {
    return 1;
  }
  if (_deterministic_sampling) {
    return _sampling_interval;
  }
  double const sampling_rate = 1.0 / static_cast<double>(_sampling_interval);
  std::exponential_distribution<> dist(sampling_rate);
  double value = dist(gen);
  constexpr int kMaxSamplingMultiplier = 20;
  const double min_value = 8.0;
  value = std::min(
      value, static_cast<double>(_sampling_interval * kMaxSamplingMultiplier));
  value = std::max(value, min_value);
  return static_cast<size_t>(value);
}

void AllocationTracker::notify_thread_start() {
  TrackerThreadLocalState *tl_state = get_tl_state();
  if (unlikely(!tl_state)) {
    tl_state = init_tl_state();
    if (!tl_state) {
      LG_DBG("Unable to start allocation profiling on thread %d",
             ddprof::gettid());
      return;
    }
  }
}

void AllocationTracker::notify_fork() {
  if (_instance) {
    _instance->_state.pid = getpid();
  }
  TrackerThreadLocalState *tl_state = get_tl_state();
  if (unlikely(!tl_state)) {
    // The state should already exist if we forked.
    // This would mean that we were not able to create the state before forking
    LG_DBG("Unable to retrieve tl state after fork thread %d",
           ddprof::gettid());
    return;
  }
  tl_state->tid = ddprof::gettid();
}

} // namespace ddprof
