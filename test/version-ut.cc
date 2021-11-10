// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

extern "C" {
#include "version.h"
}

#include <gtest/gtest.h>
#include <string>

TEST(VersionTest, VersionStr) {
  std::string expectedStr;
  expectedStr += std::to_string(VER_MAJ) + "." + std::to_string(VER_MIN) + "." +
      std::to_string(VER_PATCH);
  std::string apiStr(str_version().ptr, str_version().len);
  EXPECT_TRUE(apiStr.find(expectedStr) != std::string::npos);
}
