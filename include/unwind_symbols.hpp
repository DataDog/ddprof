#pragma once

#include "unwind_symbols.h"

#include "base_frame_symbol_lookup.hpp"
#include "common_symbol_lookup.hpp"
#include "dso_symbol_lookup.hpp"
#include "dwfl_symbol_lookup.hpp"

#include "common_mapinfo_lookup.hpp"
#include "mapinfo_lookup.hpp"

extern "C" {
#include "logger.h"
#include "stdlib.h"
}

// out of namespace as these are visible on C side
// Minimal c++ structure to keep a style close to C
struct UnwindSymbolsHdr {
  UnwindSymbolsHdr();
  void display_stats() { _stats.display(); }

  ddprof::BaseFrameSymbolLookup _base_frame_symbol_lookup;
  ddprof::CommonSymbolLookup _common_symbol_lookup;
  ddprof::DsoSymbolLookup _dso_symbol_lookup;
  ddprof::DwflSymbolLookup _dwfl_symbol_lookup;
  ddprof::SymbolTable _symbol_table;

  ddprof::CommonMapInfoLookup _common_mapinfo_lookup;
  ddprof::DwflMapInfoLookup _dwfl_mapinfo_lookup;
  ddprof::MapInfoTable _mapinfo_table;

  struct ddprof::DwflSymbolLookupStats _stats;
  symbol_lookup_setting _setting;
};

inline UnwindSymbolsHdr::UnwindSymbolsHdr() : _setting(K_CACHE_ON) {
  if (const char *env_p = std::getenv("DDPROF_CACHE_SETTING")) {
    if (strcmp(env_p, "VALIDATE") == 0) {
      // Allows to compare the accuracy of the cache
      _setting = K_CACHE_VALIDATE;
      LG_NTC("%s : Validate the cache data at every call", __FUNCTION__);
    }
  }
}

// Takes a dwarf module and an instruction pointer
// Lookup if this instruction pointer was already encountered. If not, create a
// new element in the table
DDRes dwfl_symbol_get_or_insert(struct UnwindSymbolsHdr *unwind_symbol_hdr,
                                struct Dwfl_Module *mod, ElfAddress_t newpc,
                                const ddprof::Dso &dso,
                                SymbolIdx_t *symbol_idx);
