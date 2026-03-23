#pragma once

#include <cstdint>
#include <random>
#include <span>
#include <sys/types.h>

namespace ddprof {

struct TrackerThreadLocalState {
  int64_t remaining_bytes{0}; // remaining allocation bytes until next sample
  bool remaining_bytes_initialized{false}; // false if remaining_bytes is not
                                           // initialized
  std::span<const std::byte> stack_bounds;

  pid_t tid{-1}; // cache of tid

  bool reentry_guard{false}; // prevent reentry in AllocationTracker (eg. when
                             // allocation are done inside AllocationTracker)
                             // and double counting of allocations (eg. when new
                             // calls malloc, or malloc calls mmap internally)

  bool allocation_allowed{true}; // Indicate if allocation is allowed or not
                                 // (eg. when we are in mmap hook, we
                                 // should not allocate because we might already
                                 // be inside an allocation)

  // In the choice of random generators, this one is smaller
  // - smaller than mt19937 (8 vs 5K)
  std::minstd_rand gen{std::random_device{}()};
};

} // namespace ddprof
