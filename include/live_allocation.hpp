// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
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
  };

  using PidMap = std::unordered_map<pid_t, PidStacks>;
  using WatcherVector = std::vector<PidMap>;
  WatcherVector _watcher_vector;

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

  unsigned get_nb_unmatched_deallocations() const {
    return _stats._unmatched_deallocations;
  }

  void cycle() { _stats = {}; }

private:
  // returns true if the deallocation was registered
  static bool register_deallocation(uintptr_t address, PprofStacks &stacks,
                                    AddressMap &address_map);

  // returns true if the allocation was registerd
  static bool register_allocation(const UnwindOutput &uo, uintptr_t address,
                                  int64_t value, PprofStacks &stacks,
                                  AddressMap &address_map);
  struct {
    unsigned _unmatched_deallocations = {};
  } _stats;
};

} // namespace ddprof