extern "C" {
#include "version.h"
}

#include <gtest/gtest.h>
#include <string>

TEST(VersionTest, VersionStr) {
  std::string expectedStr;
  expectedStr += std::to_string(VER_MAJ) + "." + std::to_string(VER_MIN) + "." +
      std::to_string(VER_PATCH);
  std::string apiStr(str_version());
  EXPECT_TRUE(apiStr.find(expectedStr) != std::string::npos);
}
