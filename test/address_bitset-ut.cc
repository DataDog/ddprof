// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include <gtest/gtest.h>

#include <cstdint>
#include <iostream>
#include <random>
#include <unordered_set>

#include "address_bitset.hpp"

namespace ddprof {

TEST(address_bitset, simple) {
  AddressBitset address_bitset(AddressBitset::_k_default_table_size);
  EXPECT_TRUE(address_bitset.add(0xbadbeef));
  EXPECT_FALSE(address_bitset.add(0xbadbeef));
  EXPECT_TRUE(address_bitset.remove(0xbadbeef));
}

TEST(address_bitset, many_addresses) {
  constexpr unsigned kTestElements = 100000;
  AddressBitset address_bitset(AddressBitset::_k_default_table_size);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uintptr_t> dis(
      0, std::numeric_limits<uintptr_t>::max());

  std::vector<uintptr_t> addresses;
  for (unsigned i = 0; i < kTestElements; ++i) {
    uintptr_t addr = dis(gen);
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

  constexpr size_t kTestAllocCount = 500000; // 50% load factor
  constexpr uintptr_t kAlignmentMask = 0xF;

  std::random_device rd;
  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
  std::mt19937 gen(42);
  std::uniform_int_distribution<uintptr_t> dist(0, UINTPTR_MAX);

  std::vector<uintptr_t> test_addresses;
  std::unordered_set<uintptr_t> unique_addresses;

  while (unique_addresses.size() < kTestAllocCount) {
    uintptr_t addr = dist(gen) & ~kAlignmentMask;
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

TEST(address_bitset, hash_collision_handled) {
  // Test that addresses which hash to the same slot are BOTH tracked
  // correctly (using linear probing)
  constexpr unsigned table_size = AddressBitset::_k_default_table_size;
  constexpr uintptr_t kAlignmentMask = 0xF;
  constexpr int kMaxSearchIterations = 10000000;

  AddressBitset address_bitset(table_size);

  // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
  std::mt19937 gen(42);
  std::uniform_int_distribution<uintptr_t> dist(0, UINTPTR_MAX);

  std::unordered_map<uint32_t, uintptr_t> hash_to_addr;
  uintptr_t addr1 = 0;
  uintptr_t addr2 = 0;

  // Find two addresses that hash to the same slot
  for (int i = 0; i < kMaxSearchIterations && addr1 == 0; ++i) {
    uintptr_t addr = dist(gen) & ~kAlignmentMask;
    if (addr == 0) {
      continue;
    }

    uint64_t intermediate = addr >> 4;
    auto high = static_cast<uint32_t>(intermediate >> 32);
    auto low = static_cast<uint32_t>(intermediate);
    uint32_t hash = (high ^ low) & (table_size - 1);

    if (hash_to_addr.contains(hash)) {
      addr1 = hash_to_addr[hash];
      addr2 = addr;
      break;
    }
    hash_to_addr[hash] = addr;
  }

  ASSERT_NE(addr1, 0);
  ASSERT_NE(addr2, 0);

  // Both should be added successfully (linear probing handles collision)
  EXPECT_TRUE(address_bitset.add(addr1));
  EXPECT_TRUE(address_bitset.add(addr2)); // This works now!

  // Both should be removable independently
  EXPECT_TRUE(address_bitset.remove(addr1));
  EXPECT_TRUE(address_bitset.remove(addr2));
}

// This test to tune the hash approach
// Collision rate is around 5.7%, which will have an impact on sampling
#ifdef COLLISION_TEST
// Your hash function
#  ifdef TEST_IDENTITY
inline uint64_t my_hash(uintptr_t h1) { return h1; }
#  else
inline uint64_t my_hash(uintptr_t h1) {
  h1 = h1 >> 4;
  uint32_t high = (uint32_t)(h1 >> 32);
  uint32_t low = (uint32_t)h1;
  return high ^ low;
}
#  endif

TEST(address_bitset, hash_function) {
  // Number of values to hash
  const int num_values = 1000000;

  // A large address range (33 bits)
  const uintptr_t min_address = 0x100000000;
  const uintptr_t max_address = 0x200000000;

  // Create an unordered set to store hashed values
  std::unordered_set<uint32_t> lower_bits_hashed_values;

  // Initialize random number generator
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uintptr_t> distribution(min_address,
                                                        max_address);

  // Generate and hash random values within the address range
  for (int i = 0; i < num_values; ++i) {
    uintptr_t address = distribution(gen);
    uint32_t lower_bits = my_hash(address) & (8 * 1024 * 1024 - 1);
    // Insert the lower 16 bits into the set
    lower_bits_hashed_values.insert(lower_bits);
  }

  // Calculate collision statistics
  int num_collisions = num_values - lower_bits_hashed_values.size();
  double collision_rate =
      static_cast<double>(num_collisions) / num_values * 100.0;

  std::cout << "Hash test results:" << std::endl;
  std::cout << "Number of values: " << num_values << std::endl;
  std::cout << "Number of collisions: " << num_collisions << std::endl;
  std::cout << "Collision rate: " << collision_rate << "%" << std::endl;
}
#endif
} // namespace ddprof
