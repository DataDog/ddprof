#pragma once

#include "span.hpp"

#include <cstdint>
#include <random>
#include <sys/types.h>

namespace ddprof {

struct TrackerThreadLocalState {
  int64_t remaining_bytes; // remaining allocation bytes until next sample
  bool remaining_bytes_initialized; // false if remaining_bytes is not
                                    // initialized
  ddprof::span<const byte> stack_bounds;

  // In the choice of random generators, this one is smaller
  // - smaller than mt19937 (8 vs 5K)
  std::minstd_rand _gen{std::random_device{}()};

  pid_t tid; // cache of tid

  bool reentry_guard;         // prevent reentry in AllocationTracker (eg. when
                              // allocation are done inside AllocationTracker)
  bool double_tracking_guard; // prevent mmap tracking within a malloc
};

} // namespace ddprof
