// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include <atomic>
#include <memory>
#include <stdint.h>
#include <string.h>

namespace ddprof {
class AddressBitset {
  // Number of bits is the number of addresses we can store
  // We have one address per individual bit).
  // so lets say you have 1111, you can store 4 addresses.
  // We hash the address to a number (to have an equal probability of using
  // all bits). Then we use the mask to position this address in our bitset.
  // Addr -> Hash -> Mask (to get useable bits) -> Position in the bitset
  // Note: the hashing step might be bad for cache locality.
public:
  // Publish 1 Meg as default
  constexpr static unsigned _k_default_bitset_size = 8 * 1024 * 1024;
  explicit AddressBitset(unsigned bitset_size = 0) { init(bitset_size); }
  AddressBitset(AddressBitset &&other) noexcept;
  AddressBitset &operator=(AddressBitset &&other) noexcept;

  AddressBitset(AddressBitset &other) = delete;
  AddressBitset &operator=(AddressBitset &other) = delete;

  ~AddressBitset() = default;

  // returns true if the element was inserted
  bool add(uintptr_t addr);
  // returns true if the element was removed
  bool remove(uintptr_t addr);
  void clear();
  [[nodiscard]] int count() const { return _nb_addresses; }

private:
  static constexpr unsigned _k_max_bits_ignored = 4;
  unsigned _lower_bits_ignored;
  // element type
  using Word_t = uint64_t;
  constexpr static unsigned _nb_bits_per_word = sizeof(Word_t) * 8;
  // 1 Meg divided in uint64's size
  // The probability of collision is proportional to the number of elements
  // already within the bitset
  unsigned _bitset_size = {};
  unsigned _k_nb_words = {};
  unsigned _nb_bits_mask = {};
  // We can not use an actual bitset (for atomicity reasons)
  std::unique_ptr<std::atomic<uint64_t>[]> _address_bitset;
  std::atomic<int> _nb_addresses = 0;

  void init(unsigned bitset_size);

  void move_from(AddressBitset &other) noexcept;
  // This is a kind of hash function
  // We remove the lower bits (as the alignment constraints makes them useless)
  // We fold the address
  // Then we only keep the bits that matter for the order in the bitmap
  [[nodiscard]] uint32_t hash_significant_bits(uintptr_t h1) const {
    uint64_t const intermediate = h1 >> _lower_bits_ignored;
    auto const high = static_cast<uint32_t>(intermediate >> 32);
    auto const low = static_cast<uint32_t>(intermediate);
    uint32_t const res = high ^ low;
    return res & _nb_bits_mask;
  }
};
} // namespace ddprof
