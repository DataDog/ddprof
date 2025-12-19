// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "sdt_allocation_correlator.hpp"

#include "logger.hpp"

namespace ddprof {

SDTAllocationCorrelator::SDTAllocationCorrelator() = default;
SDTAllocationCorrelator::~SDTAllocationCorrelator() = default;

void SDTAllocationCorrelator::on_malloc_entry(pid_t pid, pid_t tid,
                                              uint64_t size, uint64_t timestamp,
                                              UnwindOutput stack) {
  ++_total_entries;

  auto key = std::make_pair(pid, tid);

  // Check if there's already a pending entry for this thread
  auto it = _pending.find(key);
  if (it != _pending.end()) {
    // This means we missed the exit for the previous malloc
    // This can happen if:
    // - The malloc returned an error (NULL)
    // - We lost the exit event due to ring buffer overflow
    // - Nested allocation in signal handler (rare)
    ++_missed_exits;
    LG_DBG("Overwriting pending malloc entry for pid=%d tid=%d (missed exit?)",
           pid, tid);
  }

  // Store the new pending allocation
  _pending[key] = PendingAllocation{
      .pid = pid,
      .tid = tid,
      .size = size,
      .entry_timestamp = timestamp,
      .stack = std::move(stack),
  };

  LG_DBG("Recorded malloc entry: pid=%d tid=%d size=%lu timestamp=%lu", pid,
         tid, size, timestamp);
}

std::optional<CorrelatedAllocation>
SDTAllocationCorrelator::on_malloc_exit(pid_t pid, pid_t tid, uintptr_t ptr,
                                        uint64_t timestamp) {
  ++_total_exits;

  auto key = std::make_pair(pid, tid);
  auto it = _pending.find(key);

  if (it == _pending.end()) {
    // No matching entry found
    // This can happen if:
    // - We lost the entry event due to ring buffer overflow
    // - The profiler started between entry and exit
    // - pid/tid mismatch due to thread migration (shouldn't happen)
    ++_missed_entries;
    LG_DBG("malloc exit without entry for pid=%d tid=%d ptr=%p", pid, tid,
           reinterpret_cast<void *>(ptr));
    return std::nullopt;
  }

  PendingAllocation &pending = it->second;

  // Sanity checks
  if (timestamp < pending.entry_timestamp) {
    // Exit before entry? Clock skew or event reordering issue
    LG_DBG("malloc exit before entry for pid=%d tid=%d (clock skew?)", pid,
           tid);
    _pending.erase(it);
    ++_missed_entries;
    return std::nullopt;
  }

  // Check for stale entries (entry/exit too far apart)
  uint64_t duration = timestamp - pending.entry_timestamp;
  if (duration > kDefaultMaxCorrelationAge) {
    LG_DBG("Stale malloc entry/exit for pid=%d tid=%d (duration=%lu ns)", pid,
           tid, duration);
    _pending.erase(it);
    ++_stale_cleanups;
    return std::nullopt;
  }

  // Ignore NULL returns (failed allocations)
  if (ptr == 0) {
    LG_DBG("malloc returned NULL for pid=%d tid=%d size=%lu", pid, tid,
           pending.size);
    _pending.erase(it);
    return std::nullopt;
  }

  // Success! Create correlated allocation
  ++_successful_correlations;

  CorrelatedAllocation result{
      .size = pending.size,
      .ptr = ptr,
      .stack = std::move(pending.stack),
      .timestamp = timestamp,
  };

  _pending.erase(it);

  LG_DBG("Correlated malloc: pid=%d tid=%d size=%lu ptr=%p", pid, tid,
         result.size, reinterpret_cast<void *>(result.ptr));

  return result;
}

void SDTAllocationCorrelator::on_free_entry(pid_t pid, pid_t tid, uintptr_t ptr,
                                            uint64_t timestamp) {
  // For free, we don't need to correlate with an exit
  // Just log for debugging
  (void)pid;
  (void)tid;
  (void)ptr;
  (void)timestamp;
  LG_DBG("free entry: pid=%d tid=%d ptr=%p", pid, tid,
         reinterpret_cast<void *>(ptr));
}

size_t SDTAllocationCorrelator::cleanup_stale(uint64_t current_time,
                                              uint64_t max_age_ns) {
  size_t cleaned = 0;

  for (auto it = _pending.begin(); it != _pending.end();) {
    if (current_time > it->second.entry_timestamp &&
        current_time - it->second.entry_timestamp > max_age_ns) {
      LG_DBG("Cleaning stale pending malloc: pid=%d tid=%d age=%lu ns",
             it->second.pid, it->second.tid,
             current_time - it->second.entry_timestamp);
      it = _pending.erase(it);
      ++cleaned;
      ++_stale_cleanups;
    } else {
      ++it;
    }
  }

  return cleaned;
}

void SDTAllocationCorrelator::reset_stats() {
  _total_entries = 0;
  _total_exits = 0;
  _successful_correlations = 0;
  _missed_entries = 0;
  _missed_exits = 0;
  _stale_cleanups = 0;
}

} // namespace ddprof
