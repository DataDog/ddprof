// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "version.hpp"

#include <absl/strings/substitute.h>
#include <gtest/gtest.h>
#include <string>

namespace ddprof {

TEST(VersionTest, VersionStr) {
  std::string expectedStr =
      absl::Substitute("$0.$1.$2", VER_MAJ, VER_MIN, VER_PATCH);
  EXPECT_TRUE(str_version().starts_with(expectedStr));
}

} // namespace ddprof
