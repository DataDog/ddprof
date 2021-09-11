#pragma once

#include "unwind_symbols.h"

#include "ipinfo_lookup.hpp"
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
  ddprof::IPInfoLookup _info_lookup;
  ddprof::IPInfoTable _ipinfo_table;

  ddprof::MapInfoLookup _mapinfo_lookup;
  ddprof::MapInfoTable _mapinfo_table;

  struct ddprof::IPInfoLookupStats _stats;
  ipinfo_lookup_setting _setting;
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
