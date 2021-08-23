#include <gtest/gtest.h>

extern "C" {
#include "procutils.h"
}

TEST(ProcUtilsTest, proc_read) {

  ProcStatus procstat;
  DDRes res = proc_read(&procstat);
  ASSERT_TRUE(IsDDResOK(res));
  printf("pid: %d\n", procstat.pid);
  printf("rss: %lu\n", procstat.rss);
  printf("user: %lu\n", procstat.utime);
  printf("cuser: %lu\n", procstat.cutime);
}
