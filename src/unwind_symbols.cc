#include "unwind_symbols.hpp"

#include "ddres.h"

#include <cassert>
#include <cstdio>

////////////////
/* C Wrappers */
////////////////

extern "C" {
DDRes ipinfo_lookup_get(struct UnwindSymbolsHdr *unwind_symbol_hdr,
                        struct Dwfl_Module *mod, ElfAddress_t newpc,
                        DsoUID_t dso_id, IPInfoIdx_t *ipinfo_idx) {
  try {
    ddprof::ipinfo_lookup_get(
        unwind_symbol_hdr->_info_lookup, unwind_symbol_hdr->_stats,
        unwind_symbol_hdr->_ipinfo_table, mod, newpc, dso_id, ipinfo_idx);

    if (unwind_symbol_hdr->_setting == K_CACHE_VALIDATE) {
      if (ddprof::ipinfo_lookup_check(
              mod, newpc, unwind_symbol_hdr->_ipinfo_table[*ipinfo_idx])) {
        ++(unwind_symbol_hdr->_stats._errors);
        LG_WRN("Error from ddprof::ipinfo_lookup_check (hit nb %d)",
               unwind_symbol_hdr->_stats._hit);
        return ddres_error(DD_WHAT_UW_CACHE_ERROR);
      }
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes unwind_symbols_hdr_init(struct UnwindSymbolsHdr **unwind_symbol_hdr) {
  try {
    // considering we manipulate an opaque pointer, we need to dynamically
    // allocate the cache (in full c++ you would avoid doing this)
    *unwind_symbol_hdr = new UnwindSymbolsHdr();
  }
  CatchExcept2DDRes();
  return ddres_init();
}

// Warning this should not throw
void unwind_symbols_hdr_free(struct UnwindSymbolsHdr *unwind_symbol_hdr) {
  try {
    if (unwind_symbol_hdr) {
      unwind_symbol_hdr->display_stats();
      delete unwind_symbol_hdr;
    }
    // Should never throw
  } catch (...) {
    LG_ERR("Unexpected exception in symbolizer");
    assert(false);
  }
}

} // extern C
