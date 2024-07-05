// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "loghandle.hpp"
#include "uuid.hpp"

namespace ddprof {
TEST(Uuid, simple_class) {
  ddprof::LogHandle loghandle;
  Uuid uuid;
  std::string uuid_str = uuid.to_string();
  LG_DBG("uuid: %s", uuid_str.c_str());
  EXPECT_EQ(uuid.get_version(), 4);
  // Should be of the form
  // 07a931f2-c8b5-4527-a80a-b7405be05c68
  EXPECT_EQ(uuid_str.size(), 36);
  EXPECT_EQ(uuid_str[8], '-');
  EXPECT_EQ(uuid_str[13], '-');
  EXPECT_EQ(uuid_str[14], '4');
  EXPECT_TRUE(uuid_str[19] == '8' || uuid_str[19] == '9' ||
              uuid_str[19] == 'a' || uuid_str[19] == 'b');
  EXPECT_EQ(uuid_str[18], '-');
  EXPECT_EQ(uuid_str[23], '-');

  std::string uuid_2 = ddprof::Uuid().to_string();
  EXPECT_NE(uuid.to_string(), uuid_2);
}
} // namespace ddprof
