// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "uuid.hpp"

TEST(Uuid, create) {
  std::string uuid = dd::GenerateUuidV4();
  // Should be of the form
  // 07a931f2-c8b5-4527-a80a-b7405be05c68
  EXPECT_EQ(uuid.size(), 36);
  EXPECT_EQ(uuid[8], '-');
  EXPECT_EQ(uuid[13], '-');
  EXPECT_EQ(uuid[18], '-');
  EXPECT_EQ(uuid[23], '-');

  std::string uuid_2 = dd::GenerateUuidV4();
  EXPECT_NE(uuid, uuid_2);
}
