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
  NestedSymbolValue span100_1000(0);
  map.emplace(NestedSymbolKey{0x100, 0x1000}, span100_1000);
  NestedSymbolValue span150_300(1);
  map.emplace(NestedSymbolKey{0x150, 0x300}, span150_300);
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
  NestedSymbolValue span100_300(1);
  map.emplace(NestedSymbolKey{0x100, 0x300}, span100_300);

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
  map.emplace(NestedSymbolKey{0x1180, 0x128a}, NestedSymbolValue(34));
  // Insert inlined functions as per the log
  map.emplace(NestedSymbolKey{0x11bd, 0x11bd}, NestedSymbolValue(1));
  map.emplace(NestedSymbolKey{0x11bd, 0x11c4}, NestedSymbolValue(2));
  map.emplace(NestedSymbolKey{0x11bd, 0x11bd}, NestedSymbolValue(3));
  map.emplace(NestedSymbolKey{0x11bd, 0x11bd}, NestedSymbolValue(4));
  map.emplace(NestedSymbolKey{0x11bd, 0x11bd}, NestedSymbolValue(5));
  map.emplace(NestedSymbolKey{0x11d0, 0x1203}, NestedSymbolValue(6));
  map.emplace(NestedSymbolKey{0x11fe, 0x11fe}, NestedSymbolValue(7));
  map.emplace(NestedSymbolKey{0x11d0, 0x11d0}, NestedSymbolValue(8));

  NestedSymbolKey parent_key{0x1180, 0x1300};
  // Test for a specific address
  NestedSymbolMap::FindRes res = map.find_closest(0x11e0, parent_key);
  ASSERT_TRUE(res.second);
  EXPECT_EQ(res.first->second.get_symbol_idx(),
            6); // Expecting the most specific (deepest) symbol for this address
}

TEST(NestedSymbolMap, closest_hint) {
  LogHandle handle;
  NestedSymbolMap map;
  NestedSymbolKey parent_key{0x50, 0x1000};
  NestedSymbolValue span100_1000(0);
  map.emplace(NestedSymbolKey{0x100, 0x1000}, span100_1000);
  NestedSymbolValue span100_300(1);
  map.emplace(NestedSymbolKey{0x100, 0x300}, span100_300);
  NestedSymbolValue span300_400(2);
  map.emplace(NestedSymbolKey{0x300, 0x400}, span300_400);

  { // always return the deeper element
    NestedSymbolMap::FindRes res = map.find_closest(0x100, parent_key);
    EXPECT_TRUE(res.second);
    EXPECT_EQ(res.first->second.get_symbol_idx(), 1);

    NestedSymbolMap::FindRes res_2 =
        map.find_closest_hint(0x350, parent_key, res.first);
    EXPECT_TRUE(res_2.second);
    EXPECT_EQ(res_2.first->second.get_symbol_idx(), 2);

    NestedSymbolMap::FindRes res_3 =
        map.find_closest_hint(0x900, parent_key, res_2.first);
    EXPECT_TRUE(res_3.second);
    EXPECT_EQ(res_3.first->second.get_symbol_idx(), 0);
  }
}

} // namespace ddprof
