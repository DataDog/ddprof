#include "region_holder.hpp"

#include "loghandle.hpp"

#include <gtest/gtest.h>
#include <iostream>

namespace ddprof {
TEST(RegionHodler, Simple) {
  LogHandle log_handle;
  std::string fileName = IPC_TEST_DATA "/dso_test_data.so";
  RegionHolder reg1(fileName, 12, 0, dso::kStandard);

  RegionHolder reg2 = std::move(reg1);
  char buffer[100];

  memcpy(buffer, reg2.get_region(), reg2.get_sz());
  buffer[reg2.get_sz()] = '\0';
  LG_NTC("Read data from test file : %s", buffer);
  EXPECT_EQ(strncmp(buffer, "fake content", reg2.get_sz()), 0);
}

} // namespace ddprof
