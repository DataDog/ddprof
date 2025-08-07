// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "address_bitset.hpp"
#include "allocation_tracker_tls.hpp"
#include "ddprof_base.hpp"
#include "ddres_def.hpp"
#include "pevent.hpp"
#include "reentry_guard.hpp"
#include "unlikely.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <pthread.h>
#include <unordered_set>

namespace ddprof {

class MPSCRingBufferWriter;
struct RingBufferInfo;

class AllocationTracker {
public:
  friend class AllocationTrackerDisablerForCurrentThread;
  AllocationTracker(const AllocationTracker &) = delete;
  AllocationTracker &operator=(const AllocationTracker &) = delete;

  ~AllocationTracker() { free(); }

  enum AllocationTrackingFlags : uint8_t {
    kTrackDeallocations = 0x1,
    kDeterministicSampling = 0x2,
    kOtelProfilerMode = 0x4,
  };

  struct IntervalTimerCheck {
    [[nodiscard]] bool is_set() const {
      return callback && (initial_delay.count() > 0 || interval.count() > 0);
    }

    std::function<void()> callback;
    std::chrono::milliseconds initial_delay{0};
    std::chrono::milliseconds interval{0};
  };

  static constexpr uint32_t k_max_consecutive_failures{5};

  static void notify_thread_start();
  static void notify_fork();

  static DDRes allocation_tracking_init(uint64_t allocation_profiling_rate,
                                        uint32_t flags,
                                        uint32_t stack_sample_size,
                                        const RingBufferInfo &ring_buffer,
                                        const IntervalTimerCheck &timer_check);
  static void allocation_tracking_free();

  static inline DDPROF_NO_SANITIZER_ADDRESS void
  track_allocation_s(uintptr_t addr, size_t size,
                     TrackerThreadLocalState &tl_state);

  static inline void track_deallocation_s(uintptr_t addr,
                                          TrackerThreadLocalState &tl_state);

  static inline bool is_active();

  static inline bool is_deallocation_tracking_active();

  static TrackerThreadLocalState *init_tl_state();
  // can return null (does not init)
  static TrackerThreadLocalState *get_tl_state();

private:
  static constexpr unsigned k_ratio_max_elt_to_bitset_size = 16;

  // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
  struct TrackerState {
    void init(bool track_alloc, bool track_dealloc) {
      track_allocations = track_alloc;
      track_deallocations = track_dealloc;
      lost_count = 0;
      failure_count = 0;
      pid = getpid();
    }
    std::mutex mutex;
    std::atomic<bool> track_allocations = false;
    std::atomic<bool> track_deallocations = false;
    std::atomic<uint64_t> lost_count; // count number of lost events
    std::atomic<uint32_t> failure_count;
    std::atomic<pid_t> pid; // lazy cache of pid (0 is un-init value)
    std::atomic<PerfClock::time_point> next_check_time;
  };
  // NOLINTEND(misc-non-private-member-variables-in-classes)

  AllocationTracker();

  uint64_t next_sample_interval(std::minstd_rand &gen) const;

  DDRes init(uint64_t mem_profile_interval, uint32_t flags,
             uint32_t stack_sample_size, const RingBufferInfo &ring_buffer,
             const IntervalTimerCheck &timer_check);
  void free();

  static AllocationTracker *create_instance();

  static void delete_tl_state(void *tl_state);

  static void make_key();

  void track_allocation(uintptr_t addr, size_t size,
                        TrackerThreadLocalState &tl_state);
  void track_deallocation(uintptr_t addr, TrackerThreadLocalState &tl_state);

  DDRes push_alloc_sample(uintptr_t addr, uint64_t allocated_size,
                          TrackerThreadLocalState &tl_state);

  // If notify_needed is true, consumer should be notified
  DDRes push_lost_sample(MPSCRingBufferWriter &writer,
                         TrackerThreadLocalState &tl_state,
                         bool &notify_needed);

  DDRes push_dealloc_sample(uintptr_t addr, TrackerThreadLocalState &tl_state);

  DDRes push_clear_live_allocation(TrackerThreadLocalState &tl_state);

  void check_timer(PerfClock::time_point now,
                   TrackerThreadLocalState &tl_state);

  void free_on_consecutive_failures(bool success);

  DDPROF_NOINLINE void update_timer(PerfClock::time_point now);

  TrackerState _state;
  uint64_t _sampling_interval;
  uint32_t _stack_sample_size;
  PEvent _pevent;
  bool _deterministic_sampling;
  bool _otel_profiler_mode;

  AddressBitset _allocated_address_set;
  IntervalTimerCheck _interval_timer_check;

  // These can not be tied to the internal state of the instance.
  // The creation of the instance depends on this
  static pthread_once_t _key_once; // ensures we call key creation a single time
  static pthread_key_t _tl_state_key;

  static AllocationTracker *_instance;
};

void AllocationTracker::track_allocation_s(uintptr_t addr, size_t size,
                                           TrackerThreadLocalState &tl_state) {
  AllocationTracker *instance = _instance;

  // Be safe, if allocation tracker has not been initialized, just bail out
  // Also this avoids accessing TLS during startup which causes segfaults
  // with ASAN because ASAN installs its own wrapper around tls_get_addr,
  // which triggers allocations, reenters this same function and tls_get_addr
  // wrapper and wreaks havoc...
  if (!instance) {
    return;
  }

  tl_state.remaining_bytes += size;
  if (likely(tl_state.remaining_bytes < 0)) {
    return;
  }

  if (likely(
          instance->_state.track_allocations.load(std::memory_order_relaxed))) {
    instance->track_allocation(addr, size, tl_state);
  } else {
    // allocation tracking is disabled, reset state
    tl_state.remaining_bytes_initialized = false;
    tl_state.remaining_bytes = 0;
  }
}

void AllocationTracker::track_deallocation_s(
    uintptr_t addr, TrackerThreadLocalState &tl_state) {
  // same pattern as track_allocation
  AllocationTracker *instance = _instance;
  if (!instance) {
    return;
  }
  if (instance->_state.track_deallocations.load(std::memory_order_relaxed)) {
    instance->track_deallocation(addr, tl_state);
  }
}

bool AllocationTracker::is_active() {
  auto *instance = _instance;
  return instance && instance->_state.track_allocations;
}

bool AllocationTracker::is_deallocation_tracking_active() {
  auto *instance = _instance;
  return instance && instance->_state.track_deallocations;
}

} // namespace ddprof
