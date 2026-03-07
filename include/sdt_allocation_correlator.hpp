// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "unwind_output.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

namespace ddprof {

/// Represents a pending malloc that hasn't been matched with its exit yet
struct PendingAllocation {
  pid_t pid;
  pid_t tid;
  uint64_t size;
  uint64_t entry_timestamp;
  UnwindOutput stack; // Stack trace captured at malloc entry
};

/// Result of a successful malloc correlation
struct CorrelatedAllocation {
  uint64_t size;       // Allocation size from entry
  uintptr_t ptr;       // Returned pointer from exit
  UnwindOutput stack;  // Stack trace from entry
  uint64_t timestamp;  // Exit timestamp
};

/// Correlates malloc entry (size) with exit (pointer) events
/// 
/// When malloc is called:
/// 1. Entry probe fires with size argument -> on_malloc_entry() stores it
/// 2. Exit probe fires with returned pointer -> on_malloc_exit() correlates
/// 
/// This class maintains per-thread pending allocations and matches them.
class SDTAllocationCorrelator {
public:
  SDTAllocationCorrelator();
  ~SDTAllocationCorrelator();

  // Non-copyable, movable
  SDTAllocationCorrelator(const SDTAllocationCorrelator &) = delete;
  SDTAllocationCorrelator &operator=(const SDTAllocationCorrelator &) = delete;
  SDTAllocationCorrelator(SDTAllocationCorrelator &&) = default;
  SDTAllocationCorrelator &operator=(SDTAllocationCorrelator &&) = default;

  /// Record a malloc entry event
  /// @param pid Process ID
  /// @param tid Thread ID
  /// @param size Allocation size
  /// @param timestamp Event timestamp
  /// @param stack Stack trace at entry
  void on_malloc_entry(pid_t pid, pid_t tid, uint64_t size, uint64_t timestamp,
                       UnwindOutput stack);

  /// Process a malloc exit event and try to correlate with entry
  /// @param pid Process ID
  /// @param tid Thread ID
  /// @param ptr Returned pointer
  /// @param timestamp Event timestamp
  /// @return Correlated allocation if successful, nullopt if no matching entry
  std::optional<CorrelatedAllocation> on_malloc_exit(pid_t pid, pid_t tid,
                                                     uintptr_t ptr,
                                                     uint64_t timestamp);

  /// Record a free entry event (for deallocation tracking)
  /// This is a pass-through - just returns the info needed to track deallocation
  /// @param pid Process ID
  /// @param tid Thread ID
  /// @param ptr Pointer being freed
  /// @param timestamp Event timestamp
  void on_free_entry(pid_t pid, pid_t tid, uintptr_t ptr, uint64_t timestamp);

  /// Clean up stale pending entries that are older than max_age_ns
  /// Call this periodically to prevent memory leaks from lost events
  /// @param current_time Current timestamp
  /// @param max_age_ns Maximum age in nanoseconds
  /// @return Number of stale entries cleaned up
  size_t cleanup_stale(uint64_t current_time, uint64_t max_age_ns);

  /// Get the number of pending (unmatched) allocations
  size_t pending_count() const { return _pending.size(); }

  /// Get statistics
  uint64_t total_entries() const { return _total_entries; }
  uint64_t total_exits() const { return _total_exits; }
  uint64_t successful_correlations() const { return _successful_correlations; }
  uint64_t missed_entries() const { return _missed_entries; }
  uint64_t missed_exits() const { return _missed_exits; }
  uint64_t stale_cleanups() const { return _stale_cleanups; }

  /// Reset statistics
  void reset_stats();

  /// Default maximum correlation age (1 second)
  static constexpr uint64_t kDefaultMaxCorrelationAge = 1'000'000'000ULL;

private:
  /// Hash function for (pid, tid) pair
  struct TidHash {
    size_t operator()(const std::pair<pid_t, pid_t> &p) const {
      // Combine pid and tid into a single hash
      return std::hash<uint64_t>{}((static_cast<uint64_t>(p.first) << 32) |
                                   static_cast<uint32_t>(p.second));
    }
  };

  // Key: (pid, tid), Value: pending allocation
  // Each thread can have at most one pending malloc at a time
  std::unordered_map<std::pair<pid_t, pid_t>, PendingAllocation, TidHash>
      _pending;

  // Statistics
  uint64_t _total_entries{0};
  uint64_t _total_exits{0};
  uint64_t _successful_correlations{0};
  uint64_t _missed_entries{0}; // Exit without matching entry
  uint64_t _missed_exits{0};   // Entry overwritten before exit
  uint64_t _stale_cleanups{0}; // Entries cleaned up due to age
};

} // namespace ddprof
