// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include "address_bitset.hpp"

#include <cassert>
#include <functional>
#include <unlikely.hpp>

namespace ddprof {

bool AddressBitset::set(uintptr_t addr) {
  size_t hash_addr = std::hash<uintptr_t>().operator()(addr);
  int significant_bits = hash_addr & _nb_bits_mask;
  auto res = std::div(significant_bits, 64);
  unsigned index_array = res.quot;
  unsigned bit_offset = res.rem;
  bool success = false;
  int attempt = 0;
  do {
    uint64_t old_value = _address_bitset[index_array].load();
    if (old_value & (static_cast<uint64_t>(1) << bit_offset)) {
      // element is already set (collisions are expected)
      return false;
    }
    uint64_t new_value = old_value;
    new_value |= static_cast<uint64_t>(1) << bit_offset;
    success = _address_bitset[index_array].compare_exchange_weak(old_value,
                                                                 new_value);
  } while (unlikely(!success && attempt++ < 3));
  if (success) {
    ++_nb_elements;
  }
  return success;
}

bool AddressBitset::unset(uintptr_t addr) {
  size_t hash_addr = std::hash<uintptr_t>().operator()(addr);
  int significant_bits = hash_addr & _nb_bits_mask;
  auto res = std::div(significant_bits, 64);
  unsigned index_array = res.quot;
  unsigned bit_offset = res.rem;
  bool success = false;
  int attempt = 0;
  do {
    uint64_t old_value = _address_bitset[index_array].load();
    if (!(old_value & (static_cast<uint64_t>(1) << bit_offset))) {
      // element is not already set (unexpected?)
      break;
    }
    uint64_t new_value = old_value;
    new_value ^= static_cast<uint64_t>(1) << bit_offset;
    success = _address_bitset[index_array].compare_exchange_weak(old_value,
                                                                 new_value);
  } while (unlikely(!success) && ++attempt < 4);
  assert(attempt < 4); // This can desync our live heap view (Should not happen)
  if (success && _nb_elements.load(std::memory_order_relaxed) >= 0) {
    // a reset could hit us, just prior to decrementing
    --_nb_elements; // fetch_add - 1
  }
  return success;
}

} // namespace ddprof