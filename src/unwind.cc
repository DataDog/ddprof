#include "unwind.hpp"

extern "C" {
#include "ddres.h"
#include "libebl.h"
#include "logger.h"
#include "procutils.h"
#include "signal_helper.h"
}

#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"
#include "symbolize_dwfl.hpp"
#include "unwind_dwfl.hpp"
#include "unwind_helpers.hpp"
#include "unwind_symbols.hpp"

#define UNUSED(x) (void)(x)

namespace ddprof {

static void analyze_unwinding_error(pid_t pid, uint64_t eip) {
  // expensive operations should not be executed with NDEBUG
#ifndef NDEBUG
  Map *map = procfs_MapMatch(pid, eip);
  // kill 0 to check if the process has finished protected by NDEBUG
  if (!map)
    LG_WRN("Error getting map for [%d](0x%lx)", pid, eip);
  else {
    if (process_is_alive(pid)) {
      LG_WRN("Error unwinding %s [%d](0x%lx)", map->path, pid, eip);
    } else {
      LG_NTC("Process ended before we could unwind [%d]", pid);
    }
  }
#else
  UNUSED(pid);
  UNUSED(eip);
#endif
}

DDRes unwind_init(UnwindState *us) {
  try {
    us->symbols_hdr = new UnwindSymbolsHdr();
    us->dso_hdr = new DsoHdr();
    us->dwfl_hdr = new DwflHdr();
  }
  CatchExcept2DDRes();
  elf_version(EV_CURRENT);
  return ddres_init();
}

void unwind_free(UnwindState *us) {
  if (!us)
    return;
  delete us->symbols_hdr;
  delete us->dso_hdr;
  delete us->dwfl_hdr;
  us->symbols_hdr = nullptr;
  us->dso_hdr = nullptr;
  us->dwfl_hdr = nullptr;
}

static void unwindstate_init_sample(UnwindState *us) {
  uw_output_clear(&us->output);
  us->current_regs = us->initial_regs;
}

static void add_error_frame(UnwindState *us) {
  DsoMod dso_mod =
      update_mod(us->dso_hdr, us->dwfl, us->pid, us->current_regs.eip);
  if (!dso_mod._dso_find_res.second) {
    LG_DBG("Could not localize top-level IP: [%d](0x%lx)", us->pid,
           us->current_regs.eip);
    analyze_unwinding_error(us->pid, us->current_regs.eip);
    add_common_frame(us, CommonSymbolLookup::LookupCases::unknown_dso);
  } else {
    LG_DBG("Failed unwind: %s [%d](0x%lx)",
           dso_mod._dso_find_res.first->_filename.c_str(), us->pid,
           us->current_regs.eip);
    us->dso_hdr->_stats.incr_metric(DsoStats::kUnwindFailure,
                                    dso_mod._dso_find_res.first->_type);
    add_dso_frame(us, *dso_mod._dso_find_res.first, us->current_regs.eip);
  }
}

DDRes unwindstate__unwind(UnwindState *us) {
  DDRes res = ddres_init();
  if (us->pid != 0) { // we can not unwind pid 0
    unwindstate_init_sample(us);
    // Create or get the dwfl object associated to cache
    DwflWrapper &dwfl_wrapper = us->dwfl_hdr->get_or_insert(us->pid);
    us->dwfl = dwfl_wrapper._dwfl;
    res = ddprof::unwind_dwfl(us, dwfl_wrapper);
  }
  if (IsDDResNotOK(res)) {
    add_error_frame(us);
  }
  // Add a frame that identifies executable to which these belong
  ddprof::add_virtual_base_frame(us);
  return res;
}

void unwind_pid_free(UnwindState *us, pid_t pid) {
  us->dso_hdr->pid_free(pid);
  us->dwfl_hdr->clear_pid(pid);
  us->symbols_hdr->_base_frame_symbol_lookup.erase(pid);
}

void unwind_cycle(UnwindState *us) {
  us->symbols_hdr->display_stats();
  us->symbols_hdr->cycle();

  // clean up pids that we did not see recently
  us->dwfl_hdr->clear_unvisited();
}

} // namespace ddprof
