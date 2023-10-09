// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include "address_bitset.hpp"

#include <algorithm>
#include <bit>
#include <functional>
#include <unlikely.hpp>

namespace ddprof {

namespace {
unsigned round_up_to_power_of_two(unsigned num) {
  if (num == 0) {
    return num;
  }
  // If num is already a power of two
  if ((num & (num - 1)) == 0) {
    return num;
  }
  // not a power of two
  unsigned count = 0;
  while (num) {
    num >>= 1;
    count++;
  }
  return 1 << count;
}
} // namespace

AddressBitset::AddressBitset(AddressBitset &&other) noexcept {
  move_from(other);
}

AddressBitset &AddressBitset::operator=(AddressBitset &&other) noexcept {
  if (this != &other) {
    move_from(other);
  }
  return *this;
}

void AddressBitset::move_from(AddressBitset &other) noexcept {
  _lower_bits_ignored = other._lower_bits_ignored;
  _bitset_size = other._bitset_size;
  _k_nb_words = other._k_nb_words;
  _nb_bits_mask = other._nb_bits_mask;
  _address_bitset = std::move(other._address_bitset);
  _nb_addresses.store(other._nb_addresses.load());

  // Reset the state of 'other'
  other._bitset_size = 0;
  other._k_nb_words = 0;
  other._nb_bits_mask = 0;
  other._nb_addresses = 0;
}

void AddressBitset::init(unsigned bitset_size) {
  // Due to memory alignment, on 64 bits we can assume that the first 4
  // bits can be ignored
  _lower_bits_ignored = _k_max_bits_ignored;
  if (_address_bitset) {
    _address_bitset.reset();
  }
  _bitset_size = round_up_to_power_of_two(bitset_size);
  _k_nb_words = (_bitset_size) / (_nb_bits_per_word);
  if (_bitset_size) {
    _nb_bits_mask = _bitset_size - 1;
    _address_bitset = std::make_unique<std::atomic<uint64_t>[]>(_k_nb_words);
  }
}

bool AddressBitset::add(uintptr_t addr) {
  const uint32_t significant_bits = hash_significant_bits(addr);
  // As per nsavoire's comment, it is better to use separate operators
  // than to use the div instruction which generates an extra function call
  // Also, the usage of a power of two value allows for bit operations
  const unsigned index_array = significant_bits / _nb_bits_per_word;
  const unsigned bit_offset = significant_bits % _nb_bits_per_word;
  const Word_t bit_in_element = (1UL << bit_offset);
  // there is a possible race between checking the value
  // and setting it
  if (!(_address_bitset[index_array].fetch_or(bit_in_element) &
        bit_in_element)) {
    // check that the element was not already set
    ++_nb_addresses;
    return true;
  }
  // Collision, element was already set
  return false;
}

bool AddressBitset::remove(uintptr_t addr) {
  const int significant_bits = hash_significant_bits(addr);
  const unsigned index_array = significant_bits / _nb_bits_per_word;
  const unsigned bit_offset = significant_bits % _nb_bits_per_word;
  const Word_t bit_in_element = (1UL << bit_offset);
  if (_address_bitset[index_array].fetch_and(~bit_in_element) &
      bit_in_element) {
    _nb_addresses.fetch_sub(1, std::memory_order_relaxed);
    // in the unlikely event of a clear right at the wrong time, we could
    // have a negative number of elements (though count desyncs are acceptable)
    return true;
  }
  return false;
}

void AddressBitset::clear() {
  for (unsigned i = 0; i < _k_nb_words; ++i) {
    const Word_t original_value = _address_bitset[i].exchange(0);
    // Count number of set bits in original_value
    const int num_set_bits = std::popcount(original_value);
    if (num_set_bits > 0) {
      _nb_addresses.fetch_sub(num_set_bits, std::memory_order_relaxed);
    }
  }
}

} // namespace ddprof
