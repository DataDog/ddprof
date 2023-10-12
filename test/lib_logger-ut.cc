// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "lib/lib_logger.hpp"

namespace ddprof {

void foo_log(const std::string &str) { LOG_ONCE("%s", str.c_str()); }

TEST(LibLogger, simple) {
  testing::internal::CaptureStderr();
  LOG_ONCE("something ");
  LOG_ONCE("else ");
  foo_log("one "); // this shows
  foo_log("two "); // this does not show
  const char *some_string = "some_string";
  LOG_ONCE("three %s\n", some_string);
  std::string output = testing::internal::GetCapturedStderr();
  fprintf(stderr, "%s", output.c_str());
#ifdef NDEBUG
  EXPECT_EQ(output, "something else one three some_string\n");
#else
  EXPECT_EQ(output, "something else one two three some_string\n");
#endif
}
} // namespace ddprof
