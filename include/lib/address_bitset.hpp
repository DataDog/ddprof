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
public:
  explicit AddressBitset() {}
  ~AddressBitset();
  AddressBitset(AddressBitset &other) = delete;
  AddressBitset &operator=(AddressBitset &other) = delete;

  // returns true if the element was inserted
  bool add(uintptr_t addr);
  // returns true if the element was removed
  bool remove(uintptr_t addr);
  void clear();
  int count() const { return _nb_addresses; }

private:
  static constexpr unsigned _lower_bits_ignored = 4;
  // element type
  using Word_t = uint64_t;
  constexpr static unsigned _nb_bits_per_word = sizeof(Word_t) * 8;
  static constexpr unsigned _k_nb_words = 4096 / 64; // (64)
  static constexpr unsigned _nb_entries_per_level = 65536;  // 2^16

  struct LeafLevel {
    std::atomic<Word_t> leaf[_k_nb_words];
  };

  struct MidLevel {
    std::atomic<LeafLevel*> mid[_nb_entries_per_level];
  };

  std::atomic<MidLevel*> _top_level[_nb_entries_per_level];
  std::atomic<int> _nb_addresses;
};
} // namespace ddprof
