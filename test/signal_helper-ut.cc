// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

extern "C" {
#include "signal_helper.h"
#include <sys/types.h>
#include <unistd.h>
}

#include <gtest/gtest.h>

TEST(SignalHelperTst, ProcessIsAlive) {
  pid_t myPid = getpid();
  // Expecting that this unit test is alive
  EXPECT_TRUE(process_is_alive(myPid));
  pid_t impossiblePid = 99999 + 1;
  EXPECT_FALSE(process_is_alive(impossiblePid));
}
