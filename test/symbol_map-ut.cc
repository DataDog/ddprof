// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "symbol_map.hpp"
#include "loghandle.hpp"

namespace ddprof {

TEST(SymbolMap, Span) {
  SymbolSpan span1;
  EXPECT_EQ(span1.get_end(), 0);
  EXPECT_EQ(span1.get_symbol_idx(), -1);
  SymbolSpan span2(0x1000, 12);
  EXPECT_EQ(span2.get_end(), 0x1000);
  EXPECT_EQ(span2.get_symbol_idx(), 12);
}

TEST(SymbolMap, Map) {
  SymbolMap map;
  SymbolSpan span0_1000(0x1000, 12);
  map.emplace(0, span0_1000);
  SymbolMap::FindRes res = map.find_closest(50);
  EXPECT_TRUE(res.second);
}


TEST(SymbolMap, NestedSymbolMap) {
  LogHandle handle;
  NestedSymbolMap map;
  NestedSymbolSpan span100_1000(0x1000, 0);
  map.emplace(0x100, span100_1000);
  NestedSymbolSpan span150_300(0x300, 1, 0x100);
  map.emplace(0x150, span150_300);
  {
    NestedSymbolMap::FindRes res = map.find_closest(0x150);
    EXPECT_TRUE(res.second);
    EXPECT_EQ(res.first->second.get_symbol_idx(), 1);
  }
  {
    NestedSymbolMap::FindRes res = map.find_closest(0x400);
    EXPECT_TRUE(res.second);
    EXPECT_EQ(res.first->second.get_symbol_idx(), 0);
  }
}

} // namespace ddprof
