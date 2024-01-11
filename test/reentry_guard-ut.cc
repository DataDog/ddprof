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

} // namespace ddprof
