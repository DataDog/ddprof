// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "base_frame_symbol_lookup.hpp"
#include "common_mapinfo_lookup.hpp"
#include "common_symbol_lookup.hpp"
#include "ddres_def.hpp"
#include "dso_symbol_lookup.hpp"
#include "logger.hpp"
#include "mapinfo_lookup.hpp"
#include "runtime_symbol_lookup.hpp"

#include <cstdlib>

namespace ddprof {
struct SymbolHdr {
  explicit SymbolHdr(bool disable_symbolization = false,
                     std::string_view path_to_proc = "")
      : _runtime_symbol_lookup(path_to_proc) {}
  void display_stats() const { _dso_symbol_lookup.stats_display(); }
  void cycle() { _runtime_symbol_lookup.cycle(); }

  void clear(pid_t pid) {
    _base_frame_symbol_lookup.erase(pid);
    // mappings are only relevant in the context of a given pid.
    _mapinfo_lookup.erase(pid);
    _runtime_symbol_lookup.erase(pid);
  }

  // Cache symbol associations
  BaseFrameSymbolLookup _base_frame_symbol_lookup;
  CommonSymbolLookup _common_symbol_lookup;
  DsoSymbolLookup _dso_symbol_lookup;
  RuntimeSymbolLookup _runtime_symbol_lookup;
  // Symbol table (contains the references to strings)
  SymbolTable _symbol_table;

  // Cache mapping associations
  CommonMapInfoLookup _common_mapinfo_lookup;
  MapInfoLookup _mapinfo_lookup;

  // The mapping table
  MapInfoTable _mapinfo_table;
};

} // namespace ddprof
