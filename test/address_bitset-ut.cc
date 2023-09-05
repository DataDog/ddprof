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
  AddressBitset address_bitset(1, AddressBitset::_k_default_max_addresses);
  EXPECT_TRUE(address_bitset.set(0xbadbeef));
  EXPECT_FALSE(address_bitset.set(0xbadbeef));
  EXPECT_TRUE(address_bitset.unset(0xbadbeef));
}

TEST(address_bitset, many_addresses) {
  AddressBitset address_bitset(1, AddressBitset::_k_default_max_addresses);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uintptr_t> dis(
      0, std::numeric_limits<uintptr_t>::max());

  std::vector<uintptr_t> addresses;
  unsigned nb_elements = 100000;
  for (unsigned i = 0; i < nb_elements; ++i) {
    uintptr_t addr = dis(gen);
    if (address_bitset.set(addr)) {
      addresses.push_back(addr);
    }
  }
  EXPECT_TRUE(nb_elements - (nb_elements / 10) < addresses.size());
  for (auto addr : addresses) {
    EXPECT_TRUE(address_bitset.unset(addr));
  }
  EXPECT_EQ(0, address_bitset.nb_addresses());
}

// This test is mostly to tune the hash approach (it would slow down CI)
#ifdef COLLISION_TEST
// Your hash function
#  ifdef TEST_IDENTITY
inline uint64_t my_hash(uintptr_t h1) { return h1; }
#  else
inline uint64_t my_hash(uintptr_t h1) { return h1 >> 8; }
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
