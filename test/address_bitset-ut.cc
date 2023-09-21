// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include <gtest/gtest.h>

#include "address_bitset.hpp"
#include <cstdint>
#include <iostream>
#include <random>
#include <set>
#include <unordered_set>

namespace ddprof {

TEST(address_bitset, simple) {
  AddressBitset address_bitset{};
  EXPECT_TRUE(address_bitset.add(0xbadbeef));
  EXPECT_FALSE(address_bitset.add(0xbadbeef));
  EXPECT_TRUE(address_bitset.remove(0xbadbeef));
}

TEST(address_bitset, triple_remove) {
  AddressBitset address_bitset{};
  EXPECT_TRUE(address_bitset.add(0xbadbeef));
  EXPECT_TRUE(address_bitset.remove(0xbadbeef));
  EXPECT_FALSE(address_bitset.remove(0xbadbeef));
  EXPECT_FALSE(address_bitset.remove(0xbadbeef));
}

TEST(address_bitset, many_addresses) {
  AddressBitset address_bitset{};
  std::random_device rd;
  std::mt19937 gen(rd());
// #define WORST_CASE
#ifdef WORST_CASE
  // if addresses are too spread out, we just can not
  // keep track of everything
  std::uniform_int_distribution<uintptr_t> dis(
      0, std::numeric_limits<uintptr_t>::max());
#else
  // with more reasonable ranges, we are OK
  std::uniform_int_distribution<uintptr_t> dis(0x1000, 0x1000000);
#endif
  std::set<uintptr_t> addresses;
  unsigned nb_elements = 100000;
  for (unsigned i = 0; i < nb_elements; ++i) {
    // avoid allocations that are too close to each other
    uintptr_t addr = (dis(gen) << 4);
    auto res = addresses.insert(addr);
    if (res.second) {
      EXPECT_TRUE(address_bitset.add(addr));
    }
  }
  for (auto addr : addresses) {
    EXPECT_TRUE(address_bitset.remove(addr));
  }
  EXPECT_EQ(0, address_bitset.count());
  for (auto addr : addresses) {
    EXPECT_FALSE(address_bitset.remove(addr));
  }
}

// the aim of this test is just not blow up
TEST(address_bitset, sparse_addressing) {
  AddressBitset address_bitset{};
  std::random_device rd;
  std::mt19937 gen(rd());
  // if addresses are too spread out, we just can not
  // keep track of everything
  std::uniform_int_distribution<uintptr_t> dis(
      0, std::numeric_limits<uintptr_t>::max());
  unsigned nb_elements = 300000;
  for (unsigned i = 0; i < nb_elements; ++i) {
    // avoid allocations that are too close to each other
    uintptr_t addr = (dis(gen) << 4);
    address_bitset.add(addr);
  }
}

} // namespace ddprof
