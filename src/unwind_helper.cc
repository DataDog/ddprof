// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind_helper.hpp"

#include "ddprof_stats.hpp"
#include "ddres.hpp"
#include "dso_hdr.hpp"
#include "symbol_hdr.hpp"
#include "unwind_state.hpp"

namespace ddprof {

namespace {
void add_frame_without_mapping(UnwindState *us, SymbolIdx_t symbol_idx) {
  add_frame(symbol_idx, k_file_info_undef, k_mapinfo_idx_null, 0, 0, us);
}

} // namespace

bool is_max_stack_depth_reached(const UnwindState &us) {
  // +2 to keep room for common base frame
  return us.output.locs.size() + 2 >= kMaxStackDepth;
}

DDRes add_frame(SymbolIdx_t symbol_idx, FileInfoId_t file_info_id,
                MapInfoIdx_t map_idx, ProcessAddress_t pc,
                ProcessAddress_t elf_addr, UnwindState *us) {
  UnwindOutput *output = &us->output;
  if (output->locs.size() >= kMaxStackDepth) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_UW_MAX_DEPTH,
                          "Max stack depth reached"); // avoid overflow
  }
  if (map_idx == -1) {
    // just add an empty element for mapping info
    map_idx = us->symbol_hdr._common_mapinfo_lookup.get_or_insert(
        CommonMapInfoLookup::MappingErrors::empty,
        us->symbol_hdr._mapinfo_table);
  }
  output->locs.emplace_back(FunLoc{.ip = pc,
                                   .elf_addr = elf_addr,
                                   .file_info_id = file_info_id,
                                   .symbol_idx = symbol_idx,
                                   .map_info_idx = map_idx});
  return {};
}

void add_common_frame(UnwindState *us, SymbolErrors lookup_case) {
  add_frame_without_mapping(us,
                            us->symbol_hdr._common_symbol_lookup.get_or_insert(
                                lookup_case, us->symbol_hdr._symbol_table));
}

void add_dso_frame(UnwindState *us, const Dso &dso,
                   ElfAddress_t normalized_addr, std::string_view addr_type) {
  add_frame_without_mapping(
      us,
      us->symbol_hdr._dso_symbol_lookup.get_or_insert(
          normalized_addr, dso, us->symbol_hdr._symbol_table, addr_type));
}

void add_virtual_base_frame(UnwindState *us) {
  add_frame_without_mapping(
      us,
      us->symbol_hdr._base_frame_symbol_lookup.get_or_insert(
          us->pid, us->symbol_hdr._symbol_table,
          us->symbol_hdr._dso_symbol_lookup, us->dso_hdr));
}

void add_error_frame(const Dso *dso, UnwindState *us,
                     [[maybe_unused]] ProcessAddress_t pc,
                     SymbolErrors error_case) {
  if (dso) {
// #define ADD_ADDR_IN_SYMB // creates more elements (but adds info on
//  addresses)
#ifdef ADD_ADDR_IN_SYMB
    add_dso_frame(us, *dso, pc, "pc");
#else
    add_dso_frame(us, *dso, 0x0, "pc");
#endif
  } else {
    add_common_frame(us, error_case);
  }
  LG_DBG("Error frame (depth#%lu)", us->output.locs.size());
}
} // namespace ddprof
