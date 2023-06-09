// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "austin_symbol_lookup.hpp"
#include "base_frame_symbol_lookup.hpp"
#include "common_mapinfo_lookup.hpp"
#include "common_symbol_lookup.hpp"
#include "ddres_def.hpp"
#include "dso_symbol_lookup.hpp"
#include "dwfl_symbol_lookup.hpp"
#include "logger.hpp"
#include "mapinfo_lookup.hpp"
#include "runtime_symbol_lookup.hpp"

#include <stdlib.h>

struct SymbolHdr {
  SymbolHdr(std::string_view path_to_proc = "")
      : _runtime_symbol_lookup(path_to_proc) {}
  void display_stats() const {
    _dwfl_symbol_lookup._stats.display(_dwfl_symbol_lookup.size());
    _dso_symbol_lookup.stats_display();
  }
  void cycle() {
    _dwfl_symbol_lookup._stats.reset();
    _runtime_symbol_lookup.cycle();
  }

  void clear(pid_t pid) {
    _base_frame_symbol_lookup.erase(pid);
    // mappings are only relevant in the context of a given pid.
    _mapinfo_lookup.erase(pid);
    _runtime_symbol_lookup.erase(pid);
  }

  // Cache symbol associations
  ddprof::BaseFrameSymbolLookup _base_frame_symbol_lookup;
  ddprof::CommonSymbolLookup _common_symbol_lookup;
  ddprof::DsoSymbolLookup _dso_symbol_lookup;
  ddprof::DwflSymbolLookup _dwfl_symbol_lookup;
  ddprof::RuntimeSymbolLookup _runtime_symbol_lookup;
  ddprof::AustinSymbolLookup _austin_symbol_lookup;
  // Symbol table (contains the references to strings)
  ddprof::SymbolTable _symbol_table;

  // Cache mapping associations
  ddprof::CommonMapInfoLookup _common_mapinfo_lookup;
  ddprof::MapInfoLookup _mapinfo_lookup;

  // The mapping table
  ddprof::MapInfoTable _mapinfo_table;
};
