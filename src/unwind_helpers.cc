#include "unwind_helpers.hpp"
#include "ddres.h"
#include "dso_hdr.hpp"
#include "unwind_symbols.hpp"

namespace ddprof {
bool max_stack_depth_reached(UnwindState *us) {
  UnwindOutput *output = &us->output;
  if (output->nb_locs + 1 >= DD_MAX_STACK_DEPTH) {
    // ensure we don't overflow
    output->nb_locs = DD_MAX_STACK_DEPTH - 1;
    add_common_frame(us, CommonSymbolLookup::LookupCases::truncated_stack);
    return true;
  }
  return false;
}

static void add_virtual_frame(UnwindState *us, SymbolIdx_t symbol_idx) {
  UnwindOutput *output = &us->output;
  int64_t current_idx = output->nb_locs;
  output->locs[current_idx]._symbol_idx = symbol_idx;

  // API in lib mode should clarify this
  output->locs[current_idx].ip = 0;

  // just add an empty element for mapping info
  output->locs[current_idx]._map_info_idx =
      us->symbols_hdr->_common_mapinfo_lookup.get_or_insert(
          CommonMapInfoLookup::LookupCases::empty,
          us->symbols_hdr->_mapinfo_table);

  output->nb_locs++;
}

void add_common_frame(UnwindState *us,
                      CommonSymbolLookup::LookupCases lookup_case) {
  add_virtual_frame(us,
                    us->symbols_hdr->_common_symbol_lookup.get_or_insert(
                        lookup_case, us->symbols_hdr->_symbol_table));
}

void add_dso_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc) {
  add_virtual_frame(us,
                    us->symbols_hdr->_dso_symbol_lookup.get_or_insert(
                        pc, dso, us->symbols_hdr->_symbol_table));
}

void add_virtual_base_frame(UnwindState *us) {
  add_virtual_frame(us,
                    us->symbols_hdr->_base_frame_symbol_lookup.get_or_insert(
                        us->pid, us->symbols_hdr->_symbol_table,
                        us->symbols_hdr->_dso_symbol_lookup, *us->dso_hdr));
}
} // namespace ddprof
