// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

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
  SymbolMap map;
  SymbolSpan span0_1000(0x1000, 12);
  map.emplace(0, span0_1000);
}

} // namespace ddprof
