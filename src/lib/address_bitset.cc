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
int most_significant_bit_pos(int n) {
  if (n == 0)
    return 0;
  int msb = 0;
  n = n / 2;
  while (n != 0) {
    n = n / 2;
    msb++;
  }
  return msb;
}
} // namespace

void AddressBitset::init(int sampling_period, unsigned max_addresses) {
  _lower_bits_ignored = most_significant_bit_pos(sampling_period);
  // we avoid ignoring too many of the lower bits
  _lower_bits_ignored = std::min<int>(_k_max_bits_ignored, _lower_bits_ignored);
  if (_address_bitset) {
    _address_bitset.reset();
  }
  _nb_bits = max_addresses;
  _k_nb_elements = (_nb_bits) / (_nb_bits_per_elt);
  if (_nb_bits) {
    _nb_bits_mask = _nb_bits - 1;
    _address_bitset = std::make_unique<std::atomic<uint64_t>[]>(_k_nb_elements);
  }
}

bool AddressBitset::set(uintptr_t addr) {
  uint64_t hash_addr = remove_lower_bits(addr);
  unsigned significant_bits = hash_addr & _nb_bits_mask;
  // As per nsavoire's comment, it is better to use separate operators
  // than to use the div instruction which generates an extra function call
  // Also, the usage of a power of two value allows for bit operations
  unsigned index_array = significant_bits / 64;
  unsigned bit_offset = significant_bits % 64;
  uint64_t bit_in_element = (1UL << bit_offset);
  uint64_t old_value = _address_bitset[index_array].load();
  if (old_value & bit_in_element) {
    // element is already set (collisions are expected)
    return false;
  }
  // there is a possible race between checking the value
  // and setting it
  _address_bitset[index_array].fetch_or(bit_in_element);
  ++_nb_addresses;
  return true;
}

bool AddressBitset::unset(uintptr_t addr) {
  uint64_t hash_addr = remove_lower_bits(addr);
  int significant_bits = hash_addr & _nb_bits_mask;
  unsigned index_array = significant_bits / 64;
  unsigned bit_offset = significant_bits % 64;
  uint64_t bit_in_element = (1UL << bit_offset);
  uint64_t old_value = _address_bitset[index_array].load();
  if (!(old_value & bit_in_element)) {
    // element is not already unset de-sync
    return false;
  }
  _address_bitset[index_array].fetch_xor(bit_in_element);
  // a reset could hit us, just prior to decrementing
  // How important is avoiding a negative value? (vs avoiding this check?)
  if (likely(_nb_addresses.load(std::memory_order_relaxed) >= 0)) {
    --_nb_addresses; // fetch_add - 1
  }
  return true;
}

void AddressBitset::clear() {
  for (unsigned i = 0; i < _k_nb_elements; ++i) {
    _address_bitset[i].store(0, std::memory_order_relaxed);
  }
  _nb_addresses.store(0);
}

} // namespace ddprof
