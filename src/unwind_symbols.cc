#include "unwind_symbols.hpp"

#include "ddres.h"

#include <cassert>
#include <cstdio>

DDRes dwfl_lookup_get_or_insert(struct UnwindSymbolsHdr *unwind_symbol_hdr,
                                struct Dwfl_Module *mod,
                                ElfAddress_t process_pc, const ddprof::Dso &dso,
                                SymbolIdx_t *symbol_idx) {
  try {
    *symbol_idx = unwind_symbol_hdr->_dwfl_symbol_lookup_v2.get_or_insert(
        unwind_symbol_hdr->_symbol_table, unwind_symbol_hdr->_dso_symbol_lookup,
        mod, process_pc, dso);
  }
  CatchExcept2DDRes();
  return ddres_init();
}
