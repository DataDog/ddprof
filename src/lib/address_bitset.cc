// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include "address_bitset.hpp"

#include <algorithm>
#include <functional>
#include <unlikely.hpp>

namespace ddprof {

namespace {
unsigned round_to_power_of_two(unsigned num) {
  if (num == 0) {
    return num;
  }
  // If max_addresses is already a power of two
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

void AddressBitset::init(unsigned max_addresses) {
  // Due to memory alignment, on 64 bits we can assume that the first 4
  // bits can be ignored
  _lower_bits_ignored = _k_max_bits_ignored;
  if (_address_bitset) {
    _address_bitset.reset();
  }
  _nb_bits = round_to_power_of_two(max_addresses);
  _k_nb_words = (_nb_bits) / (_nb_bits_per_word);
  if (_nb_bits) {
    _nb_bits_mask = _nb_bits - 1;
    _address_bitset = std::make_unique<std::atomic<uint64_t>[]>(_k_nb_words);
  }
}

bool AddressBitset::add(uintptr_t addr) {
  uint32_t significant_bits = hash_significant_bits(addr);
  // As per nsavoire's comment, it is better to use separate operators
  // than to use the div instruction which generates an extra function call
  // Also, the usage of a power of two value allows for bit operations
  unsigned index_array = significant_bits / _nb_bits_per_word;
  unsigned bit_offset = significant_bits % _nb_bits_per_word;
  Word_t bit_in_element = (1UL << bit_offset);
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
  int significant_bits = hash_significant_bits(addr);
  unsigned index_array = significant_bits / _nb_bits_per_word;
  unsigned bit_offset = significant_bits % _nb_bits_per_word;
  Word_t bit_in_element = (1UL << bit_offset);
  if ((_address_bitset[index_array].fetch_xor(bit_in_element) &
       bit_in_element)) {
    _nb_addresses.fetch_sub(1, std::memory_order_relaxed);
    // in the unlikely event of a clear right at the wrong time, we could
    // have a negative number of elements (though count desyncs are acceptable)
    return true;
  }
  return false;
}

unsigned int AddressBitset::count_set_bits(Word_t w) {
  unsigned int set_bits = 0;
  while (w) {
    set_bits += w & 1;
    w >>= 1;
  }
  return set_bits;
}

void AddressBitset::clear() {
  for (unsigned i = 0; i < _k_nb_words; ++i) {
    Word_t original_value = _address_bitset[i].exchange(0);
    // Count number of set bits in original_value
    int num_set_bits = count_set_bits(original_value);
    if (num_set_bits > 0) {
      _nb_addresses.fetch_sub(num_set_bits, std::memory_order_relaxed);
    }
  }
}

} // namespace ddprof
