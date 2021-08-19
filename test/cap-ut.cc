#include <gtest/gtest.h>

#include <array>
#include <iostream>

#include "loghandle.hpp"

extern "C" {
#include "cap_display.h"
#include "logger.h"

#include <sys/capability.h>
}

namespace ddprof {

TEST(CapTest, capabilities) {
  LogHandle handle;
  DDRes res = log_capabilities(true);
  EXPECT_TRUE(IsDDResOK(res));
}

} // namespace ddprof