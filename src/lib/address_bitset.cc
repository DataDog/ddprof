// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include "address_bitset.hpp"

#include <cassert>
#include <functional>
#include <unlikely.hpp>

namespace ddprof {

namespace {
// instead of a hash function we just remove lower bits
// the assumption is that we sample, so we should have less address ranges
// that are close to each other.
// We can't use the sample period as we can still have addresses
// that are close due to sequences of allocations / frees
inline uint64_t remove_lower_bits(uintptr_t h1) { return h1 >> 8; }
} // namespace

bool AddressBitset::set(uintptr_t addr) {
  uint64_t hash_addr = remove_lower_bits(addr);
  unsigned significant_bits = hash_addr & _nb_bits_mask;
  // As per nsavoire's comment, it is better to use separate operators
  // than to use the div instruction which generates an extra function call
  // Also, the usage of a power of two value allows for bit operations
  unsigned index_array = significant_bits / 64;
  unsigned bit_offset = significant_bits % 64;
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
  } while (unlikely(!success && ++attempt < _k_max_write_attempts));
  if (success) {
    ++_nb_addresses;
  }
  return success;
}

bool AddressBitset::unset(uintptr_t addr) {
  uint64_t hash_addr = remove_lower_bits(addr);
  int significant_bits = hash_addr & _nb_bits_mask;
  auto res = std::div(significant_bits, _nb_bits_per_elt);
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
  } while (unlikely(!success && ++attempt < _k_max_write_attempts));
  assert(attempt < _k_max_write_attempts); // This can desync our live heap view
                                           // (Should not happen)
  if (success && _nb_addresses.load(std::memory_order_relaxed) >= 0) {
    // a reset could hit us, just prior to decrementing
    --_nb_addresses; // fetch_add - 1
  }
  return success;
}

void AddressBitset::clear() {
  for (unsigned i = 0; i < _k_nb_elements; ++i) {
    _address_bitset[i].store(0, std::memory_order_relaxed);
  }
  _nb_addresses.store(0);
}

} // namespace ddprof
