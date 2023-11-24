// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "loghandle.hpp"
#include "symbol_map.hpp"

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
  LogHandle handle;
  LG_DBG("Size of SymbolMap::ValueType: %lu", sizeof(SymbolMap::ValueType));
  SymbolMap map;
  SymbolSpan span0_1000(0x1000, 12);
  map.emplace(0, span0_1000);
  SymbolMap::FindRes res = map.find_closest(50);
  EXPECT_TRUE(res.second);
}

TEST(NestedSymbolMap, simple) {
  NestedSymbolKey parent_key{0x50, 0x1000};
  LogHandle handle;
  NestedSymbolMap map;
  NestedSymbolValue span100_1000(0, 0);
  map.emplace(NestedSymbolKey{0x100, 0x1000}, span100_1000);
  NestedSymbolValue span150_300(1, 0x100);
  map.emplace(NestedSymbolKey{0x150, 0x300}, span150_300);
  for (auto &el : map) {
    LG_DBG("Idx = %d", el.second.get_symbol_idx());
  }
  {
    NestedSymbolMap::FindRes res = map.find_closest(0x150, parent_key);
    EXPECT_TRUE(res.second);
    EXPECT_EQ(res.first->second.get_symbol_idx(), 1);
  }
  {
    NestedSymbolMap::FindRes res = map.find_closest(0x400, parent_key);
    EXPECT_TRUE(res.second);
    EXPECT_EQ(res.first->second.get_symbol_idx(), 0);
  }
}

TEST(NestedSymbolMap, same_addr) {
  LogHandle handle;
  NestedSymbolMap map;
  NestedSymbolKey parent_key{0x50, 0x1000};
  NestedSymbolValue span100_1000(0);
  map.emplace(NestedSymbolKey{0x100, 0x1000}, span100_1000);
  NestedSymbolValue span100_300(1, 0x100);
  map.emplace(NestedSymbolKey{0x100, 0x300}, span100_300);
  for (auto &el : map) {
    LG_DBG("Idx = %d", el.second.get_symbol_idx());
  }

  { // always return the deeper element
    NestedSymbolMap::FindRes res = map.find_closest(0x100, parent_key);
    EXPECT_TRUE(res.second);
    EXPECT_EQ(res.first->second.get_symbol_idx(), 1);
  }
}

// todo : fix bug on same start different end with multiple
TEST(NestedSymbolMap, InlinedFunctionLookup) {
  LogHandle handle;
  NestedSymbolMap map;
  // Insert main function
  map.emplace(NestedSymbolKey{0x1180, 0x128a}, NestedSymbolValue(34, 0));
  // Insert inlined functions as per the log
  map.emplace(NestedSymbolKey{0x11bd, 0x11bd}, NestedSymbolValue(1, 0x1180));
  map.emplace(NestedSymbolKey{0x11bd, 0x11c4}, NestedSymbolValue(2, 0x1180));
  map.emplace(NestedSymbolKey{0x11bd, 0x11bd}, NestedSymbolValue(3, 0x1180));
  map.emplace(NestedSymbolKey{0x11bd, 0x11bd}, NestedSymbolValue(4, 0x1180));
  map.emplace(NestedSymbolKey{0x11bd, 0x11bd}, NestedSymbolValue(5, 0x1180));
  map.emplace(NestedSymbolKey{0x11d0, 0x1203}, NestedSymbolValue(6, 0x1180));
  map.emplace(NestedSymbolKey{0x11fe, 0x11fe}, NestedSymbolValue(7, 0x11d0));
  map.emplace(NestedSymbolKey{0x11d0, 0x11d0}, NestedSymbolValue(8, 0x11d0));

  NestedSymbolKey parent_key{0x1180, 0x1300};
  // Test for a specific address
  NestedSymbolMap::FindRes res = map.find_closest(0x11e0, parent_key);
  ASSERT_TRUE(res.second);
  EXPECT_EQ(res.first->second.get_symbol_idx(),
            6); // Expecting the most specific (deepest) symbol for this address
}

} // namespace ddprof
