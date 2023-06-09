// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "symbol_table.hpp"

#include "libaustin.h"

#include <unordered_map>

namespace ddprof {

class AustinSymbolLookup {
public:
  SymbolIdx_t get_or_insert(austin_frame_t *frame, SymbolTable &symbol_table);
  void clear() {
    for (auto el : _austin_symbols) {
      _free_list.push_back(el);
    }
    _austin_symbols.clear();
  }
private:
  std::vector<SymbolIdx_t> _austin_symbols;
  std::vector<SymbolIdx_t> _free_list;
};

} // namespace ddprof
