// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "base_frame_symbol_lookup.hpp"
#include "common_symbol_lookup.hpp"
#include "dso_symbol_lookup.hpp"
#include "dwfl_symbol_lookup.hpp"

#include "common_mapinfo_lookup.hpp"
#include "mapinfo_lookup.hpp"

#include "ddres_def.h"
extern "C" {
#include "logger.h"
#include "stdlib.h"
}

struct SymbolHdr {
  SymbolHdr() {}
  void display_stats() const {
    _dwfl_symbol_lookup_v2._stats.display(_dwfl_symbol_lookup_v2.size());
  }
  void cycle() { _dwfl_symbol_lookup_v2._stats.reset(); }

  // Cache symbol associations
  ddprof::BaseFrameSymbolLookup _base_frame_symbol_lookup;
  ddprof::CommonSymbolLookup _common_symbol_lookup;
  ddprof::DsoSymbolLookup _dso_symbol_lookup;
  ddprof::DwflSymbolLookup_V2 _dwfl_symbol_lookup_v2;
  // Symbol table (contains the references to strings)
  ddprof::SymbolTable _symbol_table;

  // Cache mapping associations
  ddprof::CommonMapInfoLookup _common_mapinfo_lookup;
  ddprof::MapInfoLookup _mapinfo_lookup;

  // The mapping table
  ddprof::MapInfoTable _mapinfo_table;

  struct ddprof::DwflSymbolLookupStats _stats;
};
