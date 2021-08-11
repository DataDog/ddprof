#include <gtest/gtest.h>

#include <array>
#include <iostream>

extern "C" {
#include "cap_display.h"
#include "logger.h"

#include <sys/capability.h>
}

namespace ddprof {

class LogHandle {
public:
  LogHandle() {
    LOG_open(LOG_STDERR, nullptr);
    LOG_setlevel(LL_DEBUG);
  }
  ~LogHandle() { LOG_close(); }
};

TEST(CapTest, capabilities) {
  LogHandle handle;
  DDRes res = log_capabilities(true);
  EXPECT_TRUE(IsDDResOK(res));
}

} // namespace ddprof