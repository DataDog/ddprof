// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "sdt_allocation_correlator.hpp"
#include "loghandle.hpp"

#include <gtest/gtest.h>

namespace ddprof {

TEST(SDTAllocationCorrelatorTest, BasicCorrelation) {
  LogHandle handle;
  SDTAllocationCorrelator correlator;

  UnwindOutput stack;
  stack.pid = 123;
  stack.tid = 456;
  stack.locs.push_back({0x1234, 0x5678, 0x9abc});

  pid_t pid = 123;
  pid_t tid = 456;
  uint64_t size = 1024;
  uint64_t entry_time = 1000;
  uint64_t exit_time = 1001;
  uintptr_t ptr = 0xdeadbeef;

  // Record malloc entry
  correlator.on_malloc_entry(pid, tid, size, entry_time, stack);
  EXPECT_EQ(correlator.pending_count(), 1);
  EXPECT_EQ(correlator.total_entries(), 1);

  // Record malloc exit and correlate
  auto result = correlator.on_malloc_exit(pid, tid, ptr, exit_time);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->size, size);
  EXPECT_EQ(result->ptr, ptr);
  EXPECT_EQ(result->timestamp, exit_time);
  EXPECT_EQ(result->stack.pid, stack.pid);
  EXPECT_EQ(result->stack.tid, stack.tid);

  EXPECT_EQ(correlator.pending_count(), 0);
  EXPECT_EQ(correlator.total_exits(), 1);
  EXPECT_EQ(correlator.successful_correlations(), 1);
  EXPECT_EQ(correlator.missed_entries(), 0);
}

TEST(SDTAllocationCorrelatorTest, MissedEntry) {
  LogHandle handle;
  SDTAllocationCorrelator correlator;

  pid_t pid = 123;
  pid_t tid = 456;
  uint64_t exit_time = 1001;
  uintptr_t ptr = 0xdeadbeef;

  // Try to correlate exit without entry
  auto result = correlator.on_malloc_exit(pid, tid, ptr, exit_time);
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(correlator.missed_entries(), 1);
  EXPECT_EQ(correlator.successful_correlations(), 0);
}

TEST(SDTAllocationCorrelatorTest, OverwrittenEntry) {
  LogHandle handle;
  SDTAllocationCorrelator correlator;

  UnwindOutput stack1, stack2;
  stack1.pid = 123;
  stack1.tid = 456;
  stack1.locs.push_back({0x1234, 0x5678, 0x9abc});
  stack2.pid = 123;
  stack2.tid = 456;
  stack2.locs.push_back({0xaaaa, 0xbbbb, 0xcccc});

  pid_t pid = 123;
  pid_t tid = 456;

  // Record first entry
  correlator.on_malloc_entry(pid, tid, 1024, 1000, stack1);
  EXPECT_EQ(correlator.pending_count(), 1);

  // Record second entry on same thread - overwrites first
  correlator.on_malloc_entry(pid, tid, 2048, 1001, stack2);
  EXPECT_EQ(correlator.pending_count(), 1);
  EXPECT_EQ(correlator.missed_exits(), 1);

  // Exit should correlate with second entry
  auto result = correlator.on_malloc_exit(pid, tid, 0xbeef, 1002);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->size, 2048);
}

TEST(SDTAllocationCorrelatorTest, MultipleThreads) {
  LogHandle handle;
  SDTAllocationCorrelator correlator;

  UnwindOutput stack1, stack2;
  stack1.pid = 100;
  stack1.tid = 1;
  stack1.locs.push_back({0x1111, 0x2222, 0x3333});
  stack2.pid = 100;
  stack2.tid = 2;
  stack2.locs.push_back({0x4444, 0x5555, 0x6666});

  // Two threads allocating simultaneously
  correlator.on_malloc_entry(100, 1, 1024, 1000, stack1);
  correlator.on_malloc_entry(100, 2, 2048, 1001, stack2);
  EXPECT_EQ(correlator.pending_count(), 2);

  // Thread 2 exits first
  auto result2 = correlator.on_malloc_exit(100, 2, 0xaaaa, 1002);
  ASSERT_TRUE(result2.has_value());
  EXPECT_EQ(result2->size, 2048);
  EXPECT_EQ(correlator.pending_count(), 1);

  // Thread 1 exits
  auto result1 = correlator.on_malloc_exit(100, 1, 0xbbbb, 1003);
  ASSERT_TRUE(result1.has_value());
  EXPECT_EQ(result1->size, 1024);
  EXPECT_EQ(correlator.pending_count(), 0);

  EXPECT_EQ(correlator.successful_correlations(), 2);
}

TEST(SDTAllocationCorrelatorTest, FreeEntry) {
  LogHandle handle;
  SDTAllocationCorrelator correlator;

  pid_t pid = 123;
  pid_t tid = 456;
  uintptr_t ptr = 0xdeadbeef;
  uint64_t timestamp = 1000;

  // on_free_entry is a pass-through, just verifying it doesn't crash
  EXPECT_NO_THROW(correlator.on_free_entry(pid, tid, ptr, timestamp));
}

TEST(SDTAllocationCorrelatorTest, CleanupStale) {
  LogHandle handle;
  SDTAllocationCorrelator correlator;

  UnwindOutput stack;
  stack.pid = 123;
  stack.tid = 456;
  stack.locs.push_back({0x1234, 0x5678, 0x9abc});

  // Add entries with old timestamps
  correlator.on_malloc_entry(100, 1, 1024, 1000, stack);
  correlator.on_malloc_entry(100, 2, 2048, 2000, stack);
  correlator.on_malloc_entry(100, 3, 4096, 5000, stack);
  EXPECT_EQ(correlator.pending_count(), 3);

  // Cleanup entries older than 2 seconds from current time 6000
  uint64_t max_age = 2000; // 2000 ns
  size_t cleaned = correlator.cleanup_stale(6000, max_age);

  // Entries at 1000 and 2000 should be cleaned (older than 6000 - 2000 = 4000)
  EXPECT_EQ(cleaned, 2);
  EXPECT_EQ(correlator.pending_count(), 1);
  EXPECT_EQ(correlator.stale_cleanups(), 2);
}

TEST(SDTAllocationCorrelatorTest, NullPointerMallocExit) {
  LogHandle handle;
  SDTAllocationCorrelator correlator;

  UnwindOutput stack;
  stack.pid = 123;
  stack.tid = 456;
  stack.locs.push_back({0x1234, 0x5678, 0x9abc});

  // Entry with size
  correlator.on_malloc_entry(123, 456, 1024, 1000, stack);
  EXPECT_EQ(correlator.pending_count(), 1);

  // Exit with null pointer (malloc failed)
  auto result = correlator.on_malloc_exit(123, 456, 0, 1001);
  
  // Should still correlate even with null pointer
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->ptr, 0);
  EXPECT_EQ(result->size, 1024);
  EXPECT_EQ(correlator.pending_count(), 0);
}

TEST(SDTAllocationCorrelatorTest, ResetStats) {
  LogHandle handle;
  SDTAllocationCorrelator correlator;

  UnwindOutput stack;
  stack.pid = 123;
  stack.tid = 456;
  stack.locs.push_back({0x1234, 0x5678, 0x9abc});

  correlator.on_malloc_entry(123, 456, 1024, 1000, stack);
  correlator.on_malloc_exit(123, 456, 0xbeef, 1001);

  EXPECT_EQ(correlator.total_entries(), 1);
  EXPECT_EQ(correlator.total_exits(), 1);
  EXPECT_EQ(correlator.successful_correlations(), 1);

  correlator.reset_stats();

  EXPECT_EQ(correlator.total_entries(), 0);
  EXPECT_EQ(correlator.total_exits(), 0);
  EXPECT_EQ(correlator.successful_correlations(), 0);
}

} // namespace ddprof
