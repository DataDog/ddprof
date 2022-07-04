// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "dso_hdr.hpp"
#include "dso_symbol_lookup.hpp"
#include "symbol_table.hpp"

namespace ddprof {
class BaseFrameSymbolLookup {
public:
  SymbolIdx_t get_or_insert(pid_t pid, SymbolTable &symbol_table,
                            DsoSymbolLookup &dso_symbol_lookup,
                            DsoHdr &dso_hdr);

  // Erase symbol lookup for this pid (warning symbols still exist)
  void erase(pid_t pid) {
    _bin_map.erase(pid);
    _pid_map.erase(pid);
  }

private:
  SymbolIdx_t insert_bin_symbol(pid_t pid, SymbolTable &symbol_table,
                                DsoSymbolLookup &dso_symbol_lookup,
                                DsoHdr &dso_hdr);
  static const int k_nb_bin_lookups = 10;
  struct PidSymbol {
    explicit PidSymbol(SymbolIdx_t symb_idx)
        : _symb_idx(symb_idx), _nb_bin_lookups(1) {}
    SymbolIdx_t _symb_idx;
    int _nb_bin_lookups;
  };
  std::unordered_map<pid_t, SymbolIdx_t> _bin_map;
  // holds generic symbol for this pid and a number of lookups to keep track of
  // failures looking for a given binary
  std::unordered_map<pid_t, PidSymbol> _pid_map;
};

} // namespace ddprof
