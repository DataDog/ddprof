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
  auto iter = _frame_key_map.find(frame->key);

  if (iter != _frame_key_map.end()) {
    return iter->second;
  }

  SymbolIdx_t index = symbol_table.size();
  std::string symname = std::string(frame->scope);

  symbol_table.push_back(Symbol(symname, symname, frame->line, std::string(frame->filename)));

  _frame_key_map.emplace(frame->key, index);

  return index;
}

} // namespace ddprof
