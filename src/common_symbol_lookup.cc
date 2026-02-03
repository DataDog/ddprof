// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "common_symbol_lookup.hpp"

#include "ddog_profiling_utils.hpp"

namespace ddprof {
namespace {
Symbol symbol_from_common(SymbolErrors lookup_case,
                          const ddog_prof_ProfilesDictionary *dict) {
  return make_symbol(std::string(),
                     std::string{k_common_frame_names[lookup_case]}, 0,
                     std::string(), dict);
}
} // namespace

SymbolIdx_t
CommonSymbolLookup::get_or_insert(SymbolErrors lookup_case,
                                  SymbolTable &symbol_table,
                                  const ddog_prof_ProfilesDictionary *dict) {
  auto const it = _map.find(lookup_case);
  SymbolIdx_t symbol_idx;
  if (it != _map.end()) {
    symbol_idx = it->second;
  } else { // insert things
    symbol_idx = symbol_table.size();
    symbol_table.push_back(symbol_from_common(lookup_case, dict));
    _map.insert({lookup_case, symbol_idx});
  }
  return symbol_idx;
}
} // namespace ddprof
