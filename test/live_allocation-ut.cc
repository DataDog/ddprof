// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "live_allocation.hpp"
#include "loghandle.hpp"

#include <gtest/gtest.h>

namespace ddprof {

TEST(LiveAllocationTest, simple) {
  LogHandle handle;
  UnwindOutput uo;
  uo.pid = 123;
  uo.tid = 456;
  uo.is_incomplete = false;
  uo.locs.push_back({0x1234, 0x5678, 0x9abc});
  uo.locs.push_back({0x4321, 0x8765, 0xcba9});

  LiveAllocation live_alloc;
  int watcher_pos = 0;
  pid_t pid = 12;
  int64_t value = 10;
  int64_t nb_registered_allocs = 10;
  { // allocate 10
    uintptr_t addr = 0x10;
    for (int i=0; i<nb_registered_allocs; ++i) {
      live_alloc.register_allocation(uo, addr, value, watcher_pos, pid);
      addr+=0x10;
    }
  }
  // Check that the hash values are equal
  //  EXPECT_EQ(hash_value, expected_hash_value);
  auto &pid_map = live_alloc._watcher_vector[0];
  EXPECT_EQ(pid_map.size(), 1);
  auto &pid_stacks = pid_map[pid];
  // all allocations are registerd
  EXPECT_EQ(pid_stacks._address_map.size(), nb_registered_allocs);
  // though the stack is the same
  ASSERT_EQ(pid_stacks._unique_stacks.size(), 1);
  const auto &el = pid_stacks._unique_stacks[uo];
  EXPECT_EQ(el._value, 100);

  { // allocate 10
    uintptr_t addr = 0x10;
    for (int i = 0; i < nb_registered_allocs; ++i) {
      live_alloc.register_deallocation(addr, watcher_pos, pid);
      addr += 0x10;
    }
  }
  // all allocations are de-registerd
  EXPECT_EQ(pid_stacks._address_map.size(), 0);
  // though the stack is the same
  EXPECT_EQ(pid_stacks._unique_stacks.size(), 0);
}

TEST(LiveAllocationTest, invalid_inputs) {
  LiveAllocation live_alloc;
  int watcher_pos = 0;
  pid_t pid = 12;
  int64_t value = 10;
  UnwindOutput uo;

  // Register allocation with empty UnwindOutput
  uintptr_t addr = 0x10;
  EXPECT_NO_THROW(live_alloc.register_allocation(uo, addr, value, watcher_pos, pid));
  auto &pid_map = live_alloc._watcher_vector[0];
  auto &pid_stacks = pid_map[pid];
  // for now we don't consider them
  EXPECT_EQ(pid_stacks._address_map.size(), 0);
  EXPECT_EQ(pid_stacks._unique_stacks.size(), 0);

  // Register allocation with negative value
  uo.pid = 123;
  uo.tid = 456;
  uo.is_incomplete = false;
  uo.locs.push_back({0x1234, 0x5678, 0x9abc});
  // We will register them (though probably cause a UI bug...)
  EXPECT_NO_THROW(live_alloc.register_allocation(uo, addr, -1, watcher_pos, pid));
  // Register deallocation with invalid address
  EXPECT_NO_THROW(live_alloc.register_deallocation(0, watcher_pos, pid));
}
}

// Other cases to consider
// -- same address registered
//
