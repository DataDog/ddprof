// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "logger.hpp"
#include "unwind_output.hpp"
#include "unlikely.hpp"

#include <unordered_map>

namespace ddprof {

template <typename T>
T& access_resize(std::vector<T>& v, size_t index, const T& default_value = T()) {
  if (unlikely(index >= v.size())) {
    v.resize(index + 1, default_value);
  }
  return v[index];
}


class LiveAllocation {
public:
  void register_allocation(const UnwindOutput &stack, uintptr_t addr,
                           size_t size, int watcher_pos, pid_t pid) {
    PidMap &pid_map = access_resize(_watcher_vector, watcher_pos);
    StackMap &stack_map = pid_map[pid];
    stack_map[addr] = AllocationInfo{
        ._stack = stack, ._size = size, ._watcher_pos = watcher_pos};
  }

  void register_deallocation(uintptr_t addr, int watcher_pos, pid_t pid) {
    PidMap &pid_map = access_resize(_watcher_vector, watcher_pos);
    StackMap &stack_map = pid_map[pid];
    if (!stack_map.erase(addr)) {
      LG_DBG("Unmatched deallocation at %lx of PID%d", addr, pid);
    }
  }

  void clear_pid_for_watcher(int watcher_pos, pid_t pid) {
    PidMap &pid_map = access_resize(_watcher_vector, watcher_pos);
    pid_map[pid].clear();
  }

  void clear_pid(pid_t pid) {
    for (auto &pid_map : _watcher_vector) {
      pid_map[pid].clear();
    }
  }

  struct AllocationInfo {
    UnwindOutput _stack;
    size_t _size;
    int _watcher_pos;
  };

  using StackMap = std::unordered_map<uintptr_t, AllocationInfo>;
  using PidMap = std::unordered_map<pid_t, StackMap>;
  using WatcherVector = std::vector<PidMap>;
  WatcherVector _watcher_vector;
};

} // namespace ddprof