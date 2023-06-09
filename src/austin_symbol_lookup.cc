// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <iterator>
#include <limits>
#include <string.h>
#include <string>
#include <vector>

#include "austin_symbol_lookup.hpp"


namespace ddprof {


SymbolIdx_t AustinSymbolLookup::get_or_insert(austin_frame_t * frame, SymbolTable &symbol_table) {
  SymbolIdx_t idx = -1;
  // reuse elements to avoid growing the table
  if (!_free_list.empty()) {
    auto el = _free_list.back();
    _free_list.pop_back();
    _austin_symbols.push_back(el);
    idx = el;
  }
  if (idx == -1) {
    idx = symbol_table.size();
    symbol_table.push_back(Symbol());
  }
  std::string symname = std::string(frame->scope);
  symbol_table[idx] = Symbol(symname, symname, frame->line, std::string(frame->filename));
  return idx;
}

} // namespace ddprof
