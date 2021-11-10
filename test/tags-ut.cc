// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "tags.hpp"

#include "loghandle.hpp"

#include <algorithm>
#include <gtest/gtest.h>

namespace ddprof {

TEST(Tags, simple) {
  const char *tag_input = "mister:sanchez";
  Tags tags;
  split(tag_input, tags);
  EXPECT_EQ(tags.size(), 1);
  EXPECT_EQ(tags[0].first, "mister");
  EXPECT_EQ(tags[0].second, "sanchez");
}

TEST(Tags, bad) {

  Tags tags;
  LogHandle handle;
  {
    const char *tag_input = "something:%q!@#";
    split(tag_input, tags);
    EXPECT_EQ(tags.size(), 0);
  }
  {
    const char *tag_input = "empty:";
    split(tag_input, tags);
    EXPECT_EQ(tags.size(), 0);
  }
}
TEST(Tags, more_tags) {
  const char *tag_input = "mister:sanchez,mister:anderson,i:have,no:"
                          "imagination,for:test,values:haha";
  Tags tags;
  split(tag_input, tags);
  EXPECT_EQ(tags.size(), 6);
  EXPECT_EQ(tags[0].first, "mister");
  EXPECT_EQ(tags[0].second, "sanchez");
  EXPECT_EQ(tags[1].first, "mister");
  EXPECT_EQ(tags[1].second, "anderson");
}

TEST(Tags, user_tags) {
  LogHandle handle;

  UserTags user_tags(nullptr, 8);
  std::for_each(user_tags._tags.begin(), user_tags._tags.end(),
                [](Tag const &el) {
                  LG_DBG("Tag = %s:%s", el.first.c_str(), el.second.c_str());
                });
}
} // namespace ddprof
