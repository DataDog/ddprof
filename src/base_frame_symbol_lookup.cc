// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "base_frame_symbol_lookup.hpp"

extern "C" {
#include "logger.h"
}
#include "dso_type.hpp"
#include "string_format.hpp"

namespace ddprof {

namespace {
Symbol symbol_from_pid(pid_t pid) {
  std::string pid_str = string_format("pid_%d", pid);
  return Symbol(0, std::string(), std::string(), 0, pid_str);
}
} // namespace

SymbolIdx_t
BaseFrameSymbolLookup::insert_bin_symbol(pid_t pid, SymbolTable &symbol_table,
                                         DsoSymbolLookup &dso_symbol_lookup,
                                         const DsoHdr &dso_hdr) {
  SymbolIdx_t symbol_idx = -1;
  DsoFindRes find_res = dso_hdr.dso_find_first_std_executable(pid);
  if (find_res.second && find_res.first->_type == dso::kStandard) {
    symbol_idx = dso_symbol_lookup.get_or_insert(*find_res.first, symbol_table);
    _bin_map.insert(std::pair<pid_t, SymbolIdx_t>(pid, symbol_idx));
  } else {
    LG_NTC("Unable to find base frame for pid %d", pid);
  }
  return symbol_idx;
}

SymbolIdx_t
BaseFrameSymbolLookup::get_or_insert(pid_t pid, SymbolTable &symbol_table,
                                     DsoSymbolLookup &dso_symbol_lookup,
                                     const DsoHdr &dso_hdr) {
  auto const it_bin = _bin_map.find(pid);
  auto const it_pid = _pid_map.find(pid);

  SymbolIdx_t symbol_idx = -1;
  if (it_bin != _bin_map.end()) {
    symbol_idx = it_bin->second;
  } else {
    // attempt k nb times to look for binary info
    if (it_pid == _pid_map.end() ||
        ++it_pid->second._nb_bin_lookups < k_nb_bin_lookups) {
      symbol_idx =
          insert_bin_symbol(pid, symbol_table, dso_symbol_lookup, dso_hdr);
    }
  }
  if (symbol_idx == -1 && it_pid != _pid_map.end()) {
    // We already build a pid symbol for this pid
    symbol_idx = it_pid->second._symb_idx;
  }
  // First time we fail on this pid : insert a pid info in symbol table
  if (symbol_idx == -1) {
    symbol_idx = symbol_table.size();
    symbol_table.push_back(symbol_from_pid(pid));
    _pid_map.emplace(pid, PidSymbol(symbol_idx));
  }
  return symbol_idx;
}

} // namespace ddprof
