// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include "address_bitset.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <functional>
#include <lib_logger.hpp>
#include <unlikely.hpp>

namespace ddprof {

bool AddressBitset::add(uintptr_t addr) {
  // Extract top 16 bits for top-level index
  unsigned top_index = (addr >> 32) & 0xFFFF;

  // If the entry at this index is null, allocate and try to atomically set it
  MidLevel *expected_mid =
      _top_level[top_index].load(std::memory_order_relaxed);
  if (!expected_mid) {
    if (_nb_mid_levels >= _k_default_max_mid_levels) {
      // Every new level adds half a meg of overhead
      LOG_ALWAYS_ONCE(
          "<Warn> ddprof: Address bitset reached maximum number of mid levels\n");
      return false;
    }
    expected_mid = new MidLevel();
    MidLevel *old_mid = nullptr;
    if (!_top_level[top_index].compare_exchange_strong(old_mid, expected_mid)) {
      delete expected_mid;
      expected_mid = old_mid;
    } else {
      ++_nb_mid_levels;
    }
    assert(expected_mid);
    if (!expected_mid) {
      // something went wrong
      return false;
    }
  }

  // Extract middle 16 bits for mid-level index
  unsigned mid_index = (addr >> 16) & 0xFFFF;

  // If the entry at this mid-level index is null, allocate and try to
  // atomically set it
  LeafLevel *expected_leaf =
      expected_mid->mid[mid_index].load(std::memory_order_acquire);
  if (!expected_leaf) {
    expected_leaf = new LeafLevel();
    LeafLevel *old_leaf = nullptr;
    if (!expected_mid->mid[mid_index].compare_exchange_strong(old_leaf,
                                                              expected_leaf)) {
      delete expected_leaf;
      expected_leaf = old_leaf;
    }
    assert(expected_leaf);
    if (!expected_leaf) {
      // something went wrong
      return false;
    }
  }

  // Extract lower 16 bits and ignore lower bits (12 remain)
  unsigned leaf_index = (addr & 0xFFFF) >> _lower_bits_ignored;
  unsigned index_array = leaf_index / _nb_bits_per_word;
  assert(index_array < _k_nb_words);
  unsigned bit_offset = leaf_index % _nb_bits_per_word;
  Word_t bit_in_element = (1UL << bit_offset);

  if (!(expected_leaf->leaf[index_array].fetch_or(bit_in_element) &
        bit_in_element)) {
    ++_nb_addresses;
    return true;
  }
  return false; // Collision
}

bool AddressBitset::remove(uintptr_t addr) {
  // Extract top 16 bits for top-level index
  unsigned top_index = (addr >> 32) & 0xFFFF;

  // Try to get the mid-level pointer. If it's null, return false.
  MidLevel *mid = _top_level[top_index].load(std::memory_order_acquire);
  if (unlikely(!mid)) {
    return false;
  }

  // Extract middle 16 bits for mid-level index
  unsigned mid_index = (addr >> 16) & 0xFFFF;

  // Try to get the leaf-level pointer from the mid-level. If it's null, return
  // false.
  LeafLevel *leaf = mid->mid[mid_index].load(std::memory_order_acquire);
  if (unlikely(!leaf)) {
    return false;
  }

  // Extract lower 16 bits and ignore lower bits (12 remain)
  unsigned leaf_index = (addr & 0xFFFF) >> _lower_bits_ignored;
  unsigned index_array = leaf_index / _nb_bits_per_word;
  assert(index_array < _k_nb_words);
  unsigned bit_offset = leaf_index % _nb_bits_per_word;
  Word_t bit_in_element = (1UL << bit_offset);

  // Use fetch_and to zero the bit
  if (leaf->leaf[index_array].fetch_and(~bit_in_element) & bit_in_element) {
    _nb_addresses.fetch_sub(1, std::memory_order_relaxed);
    return true;
  }
  // Otherwise, the bit wasn't set to begin with, so return false
  return false;
}

// TODO: the performance of this clear is horrible
// For now we will avoid calling it
void AddressBitset::clear() {
  for (unsigned t = 0; t < _nb_entries_per_level; ++t) {
    MidLevel *mid = _top_level[t].load(std::memory_order_acquire);
    if (mid) { // if mid-level exists
      for (unsigned m = 0; m < _nb_entries_per_level; ++m) {
        LeafLevel *leaf = mid->mid[m].load(std::memory_order_acquire);
        if (leaf) { // if leaf-level exists
          for (unsigned l = 0; l < _k_nb_words; ++l) {
            Word_t original_value = leaf->leaf[l].exchange(0);
            // Count number of set bits in original_value
            int num_set_bits = std::popcount(original_value);
            if (num_set_bits > 0) {
              _nb_addresses.fetch_sub(num_set_bits, std::memory_order_relaxed);
            }
          }
        }
      }
    }
  }
}

AddressBitset::~AddressBitset() {
#ifdef DEBUG
  unsigned mid_count = 0;
  unsigned leaf_count = 0;
#endif
  for (unsigned t = 0; t < _nb_entries_per_level; ++t) {
    MidLevel *mid = _top_level[t].load(std::memory_order_acquire);
    if (mid) { // if mid-level exists
#ifdef DEBUG
      ++mid_count;
#endif
      for (unsigned m = 0; m < _nb_entries_per_level; ++m) {
        LeafLevel *leaf = mid->mid[m].load(std::memory_order_acquire);
        if (leaf) { // if leaf-level exists
#ifdef DEBUG
          ++leaf_count;
#endif
          delete leaf;
        }
      }
      delete mid;
    }
  }
#ifdef DEBUG
  fprintf(stderr, "Mid count = %u \n", mid_count);
  fprintf(stderr, "Leaf count = %u \n", leaf_count);
#endif
}

} // namespace ddprof
