// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "logger.hpp"
#include "unlikely.hpp"
#include "unwind_output_hash.hpp"

#include <cstddef>
#include <sys/types.h>
#include <unordered_map>

namespace ddprof {

template <typename T>
T &access_resize(std::vector<T> &v, size_t index,
                 const T &default_value = T()) {
  if (unlikely(index >= v.size())) {
    v.resize(index + 1, default_value);
  }
  return v[index];
}

class LiveAllocation {
public:
  // For allocations Value is the size
  // This is the cumulative value and count for a given stack
  struct ValueAndCount {
    int64_t _value = 0;
    int64_t _count = 0;
  };

  using PprofStacks =
      std::unordered_map<UnwindOutput, ValueAndCount, UnwindOutputHash>;

  struct ValuePerAddress {
    int64_t _value = 0;
    PprofStacks::value_type *_unique_stack = nullptr;
  };

  using AddressMap = std::unordered_map<uintptr_t, ValuePerAddress>;
  struct PidStacks {
    AddressMap _address_map;
    PprofStacks _unique_stacks;
    uint32_t _address_conflict_count = 0;
    uint32_t _tracked_address_count = 0;
  };

  using PidMap = std::unordered_map<pid_t, PidStacks>;
  using WatcherVector = std::vector<PidMap>;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
  WatcherVector _watcher_vector;

  void register_library_state(int watcher_pos, pid_t pid,
                              uint32_t address_conflict_count,
                              uint32_t tracked_addresse_count,
                              uint32_t active_shards) {
    PidMap &pid_map = access_resize(_watcher_vector, watcher_pos);
    PidStacks &pid_stacks = pid_map[pid];
    pid_stacks._address_conflict_count = address_conflict_count;
    pid_stacks._tracked_address_count = tracked_addresse_count;
    LG_NTC("<%u> PID %d: live allocations=%lu, Unique "
           "stacks=%lu, lib tracked addresses=%u, lib active shards=%u, lib address conflicts=%u",
           watcher_pos, pid, pid_stacks._address_map.size(),
           pid_stacks._unique_stacks.size(), pid_stacks._tracked_address_count,
           active_shards, pid_stacks._address_conflict_count);
  }

  // Allocation should be aggregated per stack trace
  // instead of a stack, we would have a total size for this unique stack trace
  // and a count.
  void register_allocation(const UnwindOutput &uo, uintptr_t addr, size_t size,
                           int watcher_pos, pid_t pid) {
    PidMap &pid_map = access_resize(_watcher_vector, watcher_pos);
    PidStacks &pid_stacks = pid_map[pid];
    register_allocation(uo, addr, size, pid_stacks._unique_stacks,
                        pid_stacks._address_map);
  }

  void register_deallocation(uintptr_t addr, int watcher_pos, pid_t pid) {
    PidMap &pid_map = access_resize(_watcher_vector, watcher_pos);
    PidStacks &pid_stacks = pid_map[pid];
    if (!register_deallocation(addr, pid_stacks._unique_stacks,
                               pid_stacks._address_map)) {
      ++_stats._unmatched_deallocations;
    }
  }

  void clear_pid_for_watcher(int watcher_pos, pid_t pid) {
    PidMap &pid_map = access_resize(_watcher_vector, watcher_pos);
    pid_map.erase(pid);
  }

  void clear_pid(pid_t pid) {
    for (auto &pid_map : _watcher_vector) {
      pid_map.erase(pid);
    }
  }

  [[nodiscard]] unsigned get_nb_unmatched_deallocations() const {
    return _stats._unmatched_deallocations;
  }

  [[nodiscard]] unsigned get_nb_already_existing_allocations() const {
    return _stats._already_existing_allocations;
  }

  void cycle() { _stats = {}; }

private:
  // returns true if the deallocation was registered
  static bool register_deallocation(uintptr_t address, PprofStacks &stacks,
                                    AddressMap &address_map);

  // returns true if the allocation was registerd
  bool register_allocation(const UnwindOutput &uo, uintptr_t address,
                           int64_t value, PprofStacks &stacks,
                           AddressMap &address_map);
  struct {
    unsigned _unmatched_deallocations = {};
    unsigned _already_existing_allocations = {};
  } _stats;
};

} // namespace ddprof