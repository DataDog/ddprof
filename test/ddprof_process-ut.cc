#include "ddprof_process.hpp"

#include "loghandle.hpp"

#include <gtest/gtest.h>
#include <stdio.h>
#include <unistd.h>

namespace ddprof {
TEST(DDProfProcess, cgroup) {
  LogHandle handle;
  pid_t mypid = getpid();
  Process p(mypid);
  EXPECT_NE(p._cgroup.get_id(), 0);
  LG_DBG("Cgroup id = %lu", p._cgroup.get_id());
}
} // namespace ddprof
