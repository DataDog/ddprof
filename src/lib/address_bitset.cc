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
  _nb_elements += success ? 1 : 0;
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
      return false;
    }
    uint64_t new_value = old_value;
    new_value ^= static_cast<uint64_t>(1) << bit_offset;
    success = _address_bitset[index_array].compare_exchange_weak(old_value,
                                                                 new_value);
  } while (unlikely(!success && attempt++ < 3));
  _nb_elements -= success ? 1 : 0;
  assert(_nb_elements >= 0);
  return success;
}

} // namespace ddprof