// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "symbolize_dwfl.hpp"

extern "C" {
#include "dwfl_internals.h"
#include "logger.h"
}
#include "symbol_hdr.hpp"

namespace ddprof {
DsoMod update_mod(DsoHdr *dso_hdr, Dwfl *dwfl, int pid, ProcessAddress_t pc) {
  if (!dwfl)
    return DsoMod(dso_hdr->find_res_not_found(pid));

  // Lookup DSO
  DsoMod dso_mod_res(dso_hdr->dso_find_or_backpopulate(pid, pc));
  const DsoHdr::DsoFindRes &dso_find_res = dso_mod_res._dso_find_res;
  if (!dso_find_res.second) {
    return dso_mod_res;
  }

  const Dso &dso = *dso_find_res.first;
  dso_hdr->_stats.incr_metric(DsoStats::kTargetDso, dso._type);

  if (dso.errored()) {
    LG_DBG("DSO Previously errored - mod (%s)", dso._filename.c_str());
    return dso_mod_res;
  }

  // Now that we've confirmed a separate lookup for the DSO based on procfs
  // and/or perf_event_open() "extra" events, we do two things
  // 1. Check that dwfl has a cache for this PID/pc combination
  // 2. Check that the given cache is accurate to the DSO
  dso_mod_res._dwfl_mod = dwfl_addrmodule(dwfl, pc);
  if (dso_mod_res._dwfl_mod) {
    Dwarf_Addr vm_addr;
    dwfl_module_info(dso_mod_res._dwfl_mod, 0, &vm_addr, 0, 0, 0, 0, 0);
    if (vm_addr != dso._start - dso._pgoff) {
      LG_NTC("Incoherent DSO <--> dwfl_module");
      dso_mod_res._dwfl_mod = NULL;
    }
  }

  // Try again if either if we failed to hit the dwfl cache above
  if (!dso_mod_res._dwfl_mod && dso._type == ddprof::dso::kStandard) {
    // assumption is that this string is built only once
    const std::string filepath = dso_hdr->get_binary_loc_info(dso).first;
    if (!filepath.empty()) {
      const char *dso_name = strrchr(filepath.c_str(), '/') + 1;
      // Try again within this POD's filesystem
      LG_DBG("Invalid mod (%s), PID %d (%s)", filepath.c_str(), pid,
             dwfl_errmsg(-1));
      dso_mod_res._dwfl_mod = dwfl_report_elf(
          dwfl, dso_name, filepath.c_str(), -1, dso._start - dso._pgoff, false);
    }
  }
  if (!dso_mod_res._dwfl_mod) {
    dso.flag_error();
    LG_WRN("couldn't addrmodule (%s)[0x%lx], but got DSO %s[0x%lx:0x%lx]",
           dwfl_errmsg(-1), pc, dso._filename.c_str(), dso._start, dso._end);
    return dso_mod_res;
  }

  return dso_mod_res;
}

SymbolIdx_t add_dwfl_frame(UnwindState *us, DsoMod dso_mod, ElfAddress_t pc) {
  Dwfl_Module *mod = dso_mod._dwfl_mod;
  SymbolHdr &unwind_symbol_hdr = us->symbol_hdr;
  // if we are here, we can assume we found a dso
  assert(dso_mod._dso_find_res.second);
  const Dso &dso = *dso_mod._dso_find_res.first;

  UnwindOutput *output = &us->output;
  int64_t current_loc_idx = output->nb_locs;

  output->locs[current_loc_idx]._symbol_idx =
      unwind_symbol_hdr._dwfl_symbol_lookup_v2.get_or_insert(
          unwind_symbol_hdr._symbol_table, unwind_symbol_hdr._dso_symbol_lookup,
          mod, pc, dso);
#ifdef DEBUG
  LG_NTC("Considering frame with IP : %lx / %s ", pc,
         us->symbol_hdr._symbol_table[output->locs[current_loc_idx]._symbol_idx]
             ._symname.c_str());
#endif

  output->locs[current_loc_idx].ip = pc;

  output->locs[current_loc_idx]._map_info_idx =
      us->symbol_hdr._mapinfo_lookup.get_or_insert(
          us->pid, us->symbol_hdr._mapinfo_table, dso);
  output->nb_locs++;
  return output->locs[current_loc_idx]._symbol_idx;
}

} // namespace ddprof
