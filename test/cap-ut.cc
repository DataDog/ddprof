// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "cap_display.h"
#include "logger.h"
#include "loghandle.hpp"

#include <array>
#include <iostream>
#include <sys/capability.h>

namespace ddprof {

TEST(CapTest, capabilities) {
  LogHandle handle;
  DDRes res = log_capabilities(true);
  EXPECT_TRUE(IsDDResOK(res));
}

} // namespace ddprof