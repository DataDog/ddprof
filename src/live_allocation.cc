// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "live_allocation.hpp"

#include "logger.hpp"

namespace ddprof {

void LiveAllocation::register_deallocation(uintptr_t address,
                                           PprofStacks &stacks,
                                           AddressMap &address_map) {
  // Find the ValuePerAddress object corresponding to the address
  auto map_iter = address_map.find(address);
  if (map_iter == address_map.end()) {
    // No element found, nothing to do
    // This means we lost previous events, leading to de-sync between
    // the state of the profiler and the state of the library.
    LG_DBG("Unmatched de-allocation at %lx", address);
    return;
  }
  ValuePerAddress &v = map_iter->second;

  // Decrement count and value of the corresponding PprofStacks::value_type
  // object
  if (v._unique_stack) {
    v._unique_stack->second._value -= v._value;
    if (v._unique_stack->second._count) {
      --(v._unique_stack->second._count);
    }
    if (!v._unique_stack->second._count) {
      // If count reaches 0, remove the UnwindOutput from stacks
      stacks.erase(v._unique_stack->first);
    }
  }

  // Remove the element from the address map
  address_map.erase(map_iter);
}

void LiveAllocation::register_allocation(const UnwindOutput &uo,
                                         uintptr_t address, int64_t value,
                                         PprofStacks &stacks,
                                         AddressMap &address_map) {
  if (!uo.locs.size()) {
    // avoid sending empty stacks
    LG_DBG("(LIVE_ALLOC) Avoid registering empty stack");
    return;
  }
  // Find or create the PprofStacks::value_type object corresponding to the
  // UnwindOutput
  auto iter = stacks.find(uo);
  if (iter == stacks.end()) {
    iter = stacks.emplace(uo, ValueAndCount{}).first;
  }
  PprofStacks::value_type &unique_stack = *iter;

  // Add the value to the address map
  ValuePerAddress &v = address_map[address];
  if (v._value) {
    // unexpected, we already have an allocation here
    // This means we missed a previous free
    LG_DBG("Existing allocation: %lx (cleaning up)", address);
    if (v._unique_stack) {
      // we should decrement count / value
      v._unique_stack->second._value -= v._value;
      if (v._unique_stack->second._count) {
        --(v._unique_stack->second._count);
      }
      // Should we erase the element here ?
      // only if we are sure it is not the same as the one we are inserting.
      if (v._unique_stack != &unique_stack &&
          !v._unique_stack->second._count) {
        stacks.erase(v._unique_stack->first);
      }
    }
  }

  v._value = value;
  v._unique_stack = &unique_stack;
  v._unique_stack->second._value += value;
  ++(v._unique_stack->second._count);
}

} // namespace ddprof
