#include <gtest/gtest.h>

#include <random>

#include "address_bitset.hpp"

namespace ddprof {

TEST(address_bitset, simple) {
  AddressBitset address_bitset;
  EXPECT_TRUE(address_bitset.set(0xbadbeef));
  EXPECT_FALSE(address_bitset.set(0xbadbeef));
  EXPECT_TRUE(address_bitset.unset(0xbadbeef));
}

TEST(address_bitset, many_addresses) {
  AddressBitset address_bitset;
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
  EXPECT_EQ(0, address_bitset.nb_elements());
}
} // namespace ddprof
