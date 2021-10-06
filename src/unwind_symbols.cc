#include "unwind_symbols.hpp"

#include "ddres.h"

#include <cassert>
#include <cstdio>

DDRes dwfl_symbol_get_or_insert(struct UnwindSymbolsHdr *unwind_symbol_hdr,
                                struct Dwfl_Module *mod, ElfAddress_t newpc,
                                const ddprof::Dso &dso,
                                SymbolIdx_t *symbol_idx) {
  try {
    ddprof::dwfl_symbol_get_or_insert(
        unwind_symbol_hdr->_dwfl_symbol_lookup, unwind_symbol_hdr->_stats,
        unwind_symbol_hdr->_symbol_table, mod, newpc, dso, symbol_idx);

    if (unwind_symbol_hdr->_setting == K_CACHE_VALIDATE) {
      if (ddprof::symbol_lookup_check(
              mod, newpc, unwind_symbol_hdr->_symbol_table[*symbol_idx])) {
        ++(unwind_symbol_hdr->_stats._errors);
        LG_WRN("Error from ddprof::symbol_lookup_check (hit nb %d)",
               unwind_symbol_hdr->_stats._hit);
        return ddres_error(DD_WHAT_UW_CACHE_ERROR);
      }
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}

////////////////
/* C Wrappers */
////////////////
extern "C" {

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
