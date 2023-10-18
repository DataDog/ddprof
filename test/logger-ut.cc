#include "loghandle.hpp"

#include <gtest/gtest.h>

static int call_counter = 0;

static const char *func_incr() {
  ++call_counter;
  return "foo";
}

namespace ddprof {
TEST(RegionHolder, Simple) {
  LogHandle log_handle(LL_ERROR);
  LG_WRN("Some warning that should not show %s", func_incr());
  EXPECT_EQ(call_counter, 0);
  LG_ERR("Print the foo: %s", func_incr());
  EXPECT_EQ(call_counter, 1);
}
} // namespace ddprof
