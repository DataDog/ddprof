#pragma once

#include "ddprof_defs.hpp"
#include "logger.hpp"
#include "unwind_output.hpp"

#include <unordered_map>

namespace ddprof {

class LiveAllocation {
public:
  void register_allocation(const UnwindOutput &stack, uintptr_t addr,
                           size_t size, int watcher_pos, pid_t pid) {
    StackMap &stack_map = _pid_map[pid];
    stack_map[addr] = AllocationInfo{
        ._stack = stack, ._size = size, ._watcher_pos = watcher_pos};
  }

  void register_deallocation(uintptr_t addr, pid_t pid) {
    StackMap &stack_map = _pid_map[pid];
    if (!stack_map.erase(addr)) {
      LG_DBG("Unmatched deallocation at %lx of PID%d", addr, pid);
    }
  }

  struct AllocationInfo {
    UnwindOutput _stack;
    size_t _size;
    // Should the watcher be part of the key ?
    // In theory we could watch allocations with different rules (per watcher)
    // for now I'll leave it here
    int _watcher_pos;
  };

  using StackMap = std::unordered_map<uintptr_t, AllocationInfo>;
  using PidMap = std::unordered_map<pid_t, StackMap>;

  PidMap _pid_map;
};

} // namespace ddprof