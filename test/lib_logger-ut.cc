// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "lib/lib_logger.hpp"

namespace ddprof {

void foo_log(const std::string &str) { LOG_ONCE("%s\n", str.c_str()); }

TEST(LibLogger, simple) {
  LOG_ONCE("something\n");
  LOG_ONCE("else\n");
  foo_log("one"); // this shows
  foo_log("two"); // this does not show
  const char *some_string = "some_string";
  LOG_ONCE("Some string = %s\n", some_string);
}
} // namespace ddprof
