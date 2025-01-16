// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "signal_helper.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

namespace ddprof {
TEST(SignalHelperTst, ProcessIsAlive) {
  pid_t myPid = getpid();
  // Expecting that this unit test is alive
  EXPECT_TRUE(ddprof::process_is_alive(myPid));
  pid_t impossiblePid = 99999 + 1;
  EXPECT_FALSE(process_is_alive(impossiblePid));
}

TEST(SignalHelperTst, convert_addr) {
  char buff[100];
  uintptr_t ptr = 0x12345678;
  int len = convert_addr_to_string(ptr, buff, sizeof(buff));
  EXPECT_EQ(len, 16);
  //  includes 0 chars
  EXPECT_STREQ(buff, "0000000012345678");
}

} // namespace ddprof
