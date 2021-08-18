#include <gtest/gtest.h>

extern "C" {
#include "procutils.h"
}

TEST(ProcUtilsTest, proc_read) {

  ProcStatus procstat;
  DDRes res = proc_read(&procstat);
  ASSERT_TRUE(IsDDResOK(res));
  printf("pid: %d\n", procstat.pid);
  printf("rss: %ld\n", procstat.rss);
  printf("user: %ld\n", procstat.utime);
  printf("cuser: %ld\n", procstat.cutime);
}
