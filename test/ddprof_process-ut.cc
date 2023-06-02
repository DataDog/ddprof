#include "ddprof_process.hpp"

#include "loghandle.hpp"

#include <gtest/gtest.h>
#include <stdio.h>
#include <unistd.h>

namespace ddprof {
TEST(DDProfProcess, simple_self) {
  LogHandle handle;
  pid_t mypid = getpid();
  Process p(mypid);
  EXPECT_NE(p._cgroup, Process::kCGroupIdNull);
  LG_DBG("Cgroup id = %lu", p._cgroup);
}

TEST(DDProfProcess, no_file) {
  LogHandle handle;
  Process p(1430928460);
  EXPECT_EQ(p._cgroup, Process::kCGroupIdNull);
  LG_DBG("Cgroup id = %lu", p._cgroup);
}

} // namespace ddprof
