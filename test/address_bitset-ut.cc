// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <unordered_set>

#include "address_bitset.hpp"

namespace ddprof {

namespace {
constexpr uint32_t kDeterministicSeed = 42;
} // namespace

TEST(address_bitset, simple) {
  AddressBitset address_bitset(AddressBitset::_k_default_table_size);
  EXPECT_TRUE(address_bitset.add(0xbadbeef));
  EXPECT_FALSE(address_bitset.add(0xbadbeef));
  EXPECT_TRUE(address_bitset.remove(0xbadbeef));
}

TEST(address_bitset, many_addresses) {
#ifdef __SANITIZE_ADDRESS__
  constexpr unsigned kTestElements = 5000;
#else
  constexpr unsigned kTestElements = 10000;
#endif
  AddressBitset address_bitset(AddressBitset::_k_default_table_size);
  std::random_device rd;
  std::mt19937 gen(rd());

  // Keep addresses within same chunk to avoid expensive table creation
  constexpr uintptr_t kAlignmentMask = 0xF;
  constexpr uintptr_t kChunkMask = (1ULL << AddressBitset::_k_chunk_shift) - 1;
  constexpr uintptr_t kBaseAddr = 0x7f0000000000ULL; // Typical heap range
  std::uniform_int_distribution<uintptr_t> dis(0, kChunkMask);

  std::vector<uintptr_t> addresses;
  for (unsigned i = 0; i < kTestElements; ++i) {
    uintptr_t addr =
        kBaseAddr + (dis(gen) & ~kAlignmentMask); // 16-byte aligned
    if (address_bitset.add(addr)) {
      addresses.push_back(addr);
    }
  }
  EXPECT_TRUE(kTestElements - (kTestElements / 10) < addresses.size());
  for (auto addr : addresses) {
    EXPECT_TRUE(address_bitset.remove(addr));
  }
}

TEST(address_bitset, no_false_collisions) {
  // With the new open addressing implementation, we should have NO false
  // collisions (unlike the old bitset which had ~6% collision rate)
  AddressBitset address_bitset(AddressBitset::_k_default_table_size);

#ifdef __SANITIZE_ADDRESS__
  constexpr size_t kTestAllocCount = 5000; // ~8% load factor
#else
  constexpr size_t kTestAllocCount = 20000; // ~30% load factor
#endif
  constexpr uintptr_t kAlignmentMask = 0xF;

  std::random_device rd;
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
  std::mt19937 gen(kDeterministicSeed);

  // Keep addresses within same chunk to avoid expensive table creation
  constexpr uintptr_t kChunkMask = (1ULL << AddressBitset::_k_chunk_shift) - 1;
  constexpr uintptr_t kBaseAddr = 0x7f0000000000ULL; // Typical heap range
  std::uniform_int_distribution<uintptr_t> dist(0, kChunkMask);

  std::vector<uintptr_t> test_addresses;
  std::unordered_set<uintptr_t> unique_addresses;

  while (unique_addresses.size() < kTestAllocCount) {
    uintptr_t addr = kBaseAddr + (dist(gen) & ~kAlignmentMask);
    if (addr != 0 && unique_addresses.insert(addr).second) {
      test_addresses.push_back(addr);
    }
  }

  // Add all addresses - should succeed with no false collisions
  int add_failures = 0;
  for (auto addr : test_addresses) {
    if (!address_bitset.add(addr)) {
      add_failures++;
    }
  }

  EXPECT_EQ(add_failures, 0) << "Expected NO false collisions";

  // Remove all addresses - should all succeed
  int remove_failures = 0;
  for (auto addr : test_addresses) {
    if (!address_bitset.remove(addr)) {
      remove_failures++;
    }
  }

  EXPECT_EQ(remove_failures, 0);
}

} // namespace ddprof
