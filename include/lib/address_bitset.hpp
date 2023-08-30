#pragma once

#include <array>
#include <atomic>
#include <stdint.h>
#include <string.h>

namespace ddprof {
class AddressBitset {
public:
  // returns true if the element was inserted
  bool set(uintptr_t addr);
  // returns true if the element was removed
  bool unset(uintptr_t addr);
  void clear() {
    for (auto& element : _address_bitset) {
      element.store(0, std::memory_order_relaxed);
    }
    _nb_elements.store(0);
  };
  int nb_elements() const { return _nb_elements; }

private:
  // 1 Meg divided in uint64's size
  constexpr static unsigned _nb_bits = 8 * 1024 * 1024;
  constexpr static unsigned _k_nb_elements = (_nb_bits) / (64);
  constexpr static unsigned _nb_bits_mask = _nb_bits - 1;
  // We can not use an actual bitset (for atomicity reasons)
  std::array<std::atomic<uint64_t>, _k_nb_elements> _address_bitset = {};
  std::atomic<int> _nb_elements = 0;
};
} // namespace ddprof
