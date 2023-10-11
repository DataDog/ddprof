// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "lib/reentry_guard.hpp"
#include "syscalls.hpp"

namespace ddprof {

TEST(ReentryGuardTest, basic) {
  bool reentry_guard = false;
  {
    ReentryGuard guard(&reentry_guard);
    EXPECT_TRUE(static_cast<bool>(guard));
    EXPECT_TRUE(reentry_guard);
  }
  EXPECT_FALSE(reentry_guard);
}

TEST(ReentryGuardTest, null_init) {
  ReentryGuard guard(nullptr);
  EXPECT_FALSE(guard);
}

TEST(TLReentryGuardTest, basic) {
  ThreadEntries entries;
  pid_t tid = gettid();

  {
    TLReentryGuard guard(entries, tid);
    EXPECT_TRUE(static_cast<bool>(guard));
    // Reenter a second time
    TLReentryGuard guard2(entries, tid);
    EXPECT_FALSE(static_cast<bool>(guard2));
  }
}

TEST(TLReentryGuardTest, many_threads) {
  ThreadEntries entries;
  std::vector<std::thread> threads;
  for (int i = 0; i < 100; ++i) {
    threads.emplace_back([&]() {
      pid_t tid = gettid();
      TLReentryGuard guard(entries, tid);
      EXPECT_TRUE(static_cast<bool>(guard));
      // Sleep to simulate work
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    });
  }
  // Join all threads
  for (auto &thread : threads) {
    thread.join();
  }
  // Check that all entries are reset
  for (size_t i = 0; i < ThreadEntries::max_threads; ++i) {
    EXPECT_EQ(entries.get_entry(i).load(), -1);
  }
}

TEST(TLReentryGuardTest, reqcuisition_many_threads) {
  ThreadEntries entries;
  std::vector<std::thread> threads;
  for (int i = 0; i < 100; ++i) {
    threads.emplace_back([&]() {
      pid_t tid = gettid();

      // First acquisition
      {
        TLReentryGuard guard(entries, tid);
        EXPECT_TRUE(static_cast<bool>(guard));
        // Sleep to simulate work
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      // Re-acquisition
      {
        TLReentryGuard guard(entries, tid);
        EXPECT_TRUE(static_cast<bool>(guard));
      }
    });
  }

  // Join all threads
  for (auto &thread : threads) {
    thread.join();
  }

  // Check that all entries are reset
  for (size_t i = 0; i < ThreadEntries::max_threads; ++i) {
    EXPECT_EQ(entries.get_entry(i).load(), -1);
  }
}

} // namespace ddprof
