#include "unwind_symbols.hpp"

#include "ddres.h"

#include <cassert>
#include <cstdio>

////////////////
/* C Wrappers */
////////////////

extern "C" {
DDRes ipinfo_lookup_get(struct UnwindSymbolsHdr *cache_hdr,
                        struct Dwfl_Module *mod, ElfAddress_t newpc, pid_t pid,
                        IPInfoIdx_t *ipinfo_idx) {
  try {
    ddprof::ipinfo_lookup_get(cache_hdr->_info_lookup, cache_hdr->_stats,
                              cache_hdr->_ipinfo_table, mod, newpc, pid,
                              ipinfo_idx);

    if (cache_hdr->_setting == K_CACHE_VALIDATE) {
      if (ddprof::ipinfo_lookup_check(mod, newpc,
                                      cache_hdr->_ipinfo_table[*ipinfo_idx])) {
        ++(cache_hdr->_stats._errors);
        LG_WRN("Error from ddprof::ipinfo_lookup_check (hit nb %d)",
               cache_hdr->_stats._hit);
        return ddres_error(DD_WHAT_UW_CACHE_ERROR);
      }
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes mapinfo_lookup_get(struct UnwindSymbolsHdr *cache_hdr,
                         const Dwfl_Module *mod, MapInfoIdx_t *map_info_idx) {
  try {
    ddprof::mapinfo_lookup_get(cache_hdr->_mapinfo_lookup,
                               cache_hdr->_mapinfo_table, mod, map_info_idx);
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes unwind_symbols_hdr_clear(struct UnwindSymbolsHdr *cache_hdr) {
  try {
    cache_hdr->_info_lookup.clear();
    cache_hdr->_ipinfo_table.clear();
    cache_hdr->_mapinfo_lookup.clear();
    cache_hdr->_stats.display();
    cache_hdr->_stats.reset();
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes unwind_symbols_hdr_init(struct UnwindSymbolsHdr **cache_hdr) {
  try {
    // considering we manipulate an opaque pointer, we need to dynamically
    // allocate the cache (in full c++ you would avoid doing this)
    *cache_hdr = new UnwindSymbolsHdr();
  }
  CatchExcept2DDRes();
  return ddres_init();
}

// Warning this should not throw
void unwind_symbols_hdr_free(struct UnwindSymbolsHdr *cache_hdr) {
  try {
    if (cache_hdr) {
      cache_hdr->display_stats();
      delete cache_hdr;
    }
    // Should never throw
  } catch (...) {
    LG_ERR("Unexpected exception in symbolizer");
    assert(false);
  }
}

} // extern C
