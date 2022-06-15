// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "ddres.hpp"
#include "loghandle.hpp"
#include "sys_utils.hpp"

namespace ddprof {

TEST(SysUtils, read_int_from_file) {
  LogHandle log_handle;
  {
    int val = 0;
    const char *file_path = UNIT_TEST_DATA "/test_int_value.txt";
    DDRes res = sys_read_int_from_file(file_path, val);
    EXPECT_TRUE(IsDDResOK(res));
    EXPECT_EQ(val, 42);
  }
  {
    int val = 0;
    const char *file_path = UNIT_TEST_DATA "/no_int_in_file.txt";
    DDRes res = sys_read_int_from_file(file_path, val);
    EXPECT_TRUE(IsDDResNotOK(res));
  }
}

TEST(SysUtils, perf_event_paranoid) {
  {
    LogHandle log_handle;
    int perf_event_paranoid;
    DDRes res = sys_perf_event_paranoid(perf_event_paranoid);
    EXPECT_TRUE(IsDDResOK(res));
    printf("perf_event_paranoid = %d\n", perf_event_paranoid);
  }
}
} // namespace ddprof
