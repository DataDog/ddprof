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

void add_common_frame(UnwindState *us,
                      CommonSymbolLookup::LookupCases lookup_case) {
  UnwindOutput *output = &us->output;
  int64_t current_idx = output->nb_locs;
  output->locs[current_idx]._symbol_idx =
      us->symbols_hdr->_common_symbol_lookup.get_or_insert(
          lookup_case, us->symbols_hdr->_symbol_table);

  // API in lib mode should clarify this
  output->locs[current_idx].ip = 0;

  // just add an empty element for mapping info
  output->locs[current_idx]._map_info_idx =
      us->symbols_hdr->_common_mapinfo_lookup.get_or_insert(
          CommonMapInfoLookup::LookupCases::empty,
          us->symbols_hdr->_mapinfo_table);

  output->nb_locs++;
}

void add_dso_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc) {
  UnwindOutput *output = &us->output;
  int64_t current_idx = output->nb_locs;
  output->locs[current_idx]._symbol_idx =
      us->symbols_hdr->_dso_symbol_lookup.get_or_insert(
          pc, dso, us->symbols_hdr->_symbol_table);

  output->locs[current_idx].ip = 0;

  // todo : although we could add mapping, it is not really used
  // just add an empty element for mapping info
  output->locs[current_idx]._map_info_idx =
      us->symbols_hdr->_common_mapinfo_lookup.get_or_insert(
          CommonMapInfoLookup::LookupCases::empty,
          us->symbols_hdr->_mapinfo_table);

  output->nb_locs++;
}

DDRes add_virtual_base_frame(UnwindState *us) {
  pid_t pid = us->pid;
  // heuristic: base frame is the first loaded (lowest address)
  DsoFindRes find_res = us->dso_hdr->dso_find_first(pid);
  if (find_res.second) {
    add_dso_frame(us, *find_res.first, find_res.first->_start);
    return ddres_init();
  }
  LG_WRN("Unable to find base frame for pid %d", pid);
  return ddres_warn(DD_WHAT_UW_ERROR);
}
} // namespace ddprof
