#include "unwind_dwfl.hpp"

extern "C" {
#include "ddprof_stats.h"
#include "ddres.h"
#include "dwfl_internals.h"
#include "logger.h"
#include "procutils.h"
#include "signal_helper.h"

#include <dwarf.h>
}

#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"
#include "unwind_helpers.hpp"
#include "unwind_symbols.hpp"

#define UNUSED(x) (void)(x)

extern "C" {
pid_t next_thread(Dwfl *, void *, void **);
bool set_initial_registers(Dwfl_Thread *, void *);
int frame_cb(Dwfl_Frame *, void *);
int tid_cb(Dwfl_Thread *, void *);
bool memory_read(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result, void *arg);
}

namespace ddprof {
// Structure to group Dso and Mod
struct DsoMod {
  explicit DsoMod(DsoFindRes find_res)
      : _dso_find_res(find_res), _dwfl_mod(nullptr) {}
  DsoFindRes _dso_find_res;
  Dwfl_Module *_dwfl_mod;
};

pid_t next_thread(Dwfl *dwfl, void *arg, void **thread_argp) {
  (void)dwfl;
  if (*thread_argp != NULL) {
    return 0;
  }
  struct UnwindState *us = (UnwindState *)arg;
  *thread_argp = arg;
  return us->pid;
}

bool set_initial_registers(Dwfl_Thread *thread, void *arg) {
  struct UnwindState *us = (UnwindState *)arg;
  Dwarf_Word regs[17] = {0};

  // I only save three lol
  regs[6] = us->ebp;
  regs[7] = us->esp;
  regs[16] = us->eip;

  return dwfl_thread_state_registers(thread, 0, 17, regs);
}

static DDRes add_dwfl_frame(UnwindState *us, DsoMod dso_mod, ElfAddress_t pc) {
  Dwfl_Module *mod = dso_mod._dwfl_mod;
  // if we are here, we can assume we found a dso
  assert(dso_mod._dso_find_res.second);
  const Dso &dso = *dso_mod._dso_find_res.first;

  UnwindOutput *output = &us->output;
  int64_t current_idx = output->nb_locs;

  DDRes cache_status = dwfl_lookup_get_or_insert(
      us->symbols_hdr, mod, pc, dso, &output->locs[current_idx]._symbol_idx);
  if (IsDDResNotOK(cache_status)) {
    LG_DBG("Error from dwflmod_cache_status");
    return cache_status;
  }

  output->locs[current_idx].ip = pc;

  output->locs[current_idx]._map_info_idx =
      us->symbols_hdr->_mapinfo_lookup.get_or_insert(
          us->pid, us->symbols_hdr->_mapinfo_table, dso);
  output->nb_locs++;
  return ddres_init();
}

/// memory_read as per prototype define in libdwfl
bool memory_read(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result, void *arg) {
  (void)dwfl;
  struct UnwindState *us = (UnwindState *)arg;

  // Check for overflow, which won't be captured by the checks below.  Sometimes
  // addr is un-physically high and we don't know why yet.
  if (addr > addr + sizeof(Dwarf_Word)) {
    LG_WRN("Overflow in addr 0x%lx", addr);
    return false;
  }

  uint64_t sp_start = us->esp;
  uint64_t sp_end = sp_start + us->stack_sz;

  if (addr < sp_start || addr + sizeof(Dwarf_Word) > sp_end) {
    // If we're here, we're not in the stack.  We should interpet addr as an
    // address in VM, not as a file offset.
    // Strongly assumes we're also in an executable region?
    DsoFindRes find_res =
        us->dso_hdr->pid_read_dso(us->pid, result, sizeof(Dwarf_Word), addr);
    if (!find_res.second) {
      // Some regions are not handled
      LG_DBG("Couldn't get read 0x%lx from %d, (0x%lx, 0x%lx)[%p, %p]", addr,
             us->pid, sp_start, sp_end, us->stack, us->stack + us->stack_sz);
    }
    return find_res.second;
  }

  // If we're here, we're going to read from the stack.  Just the same, we need
  // to protect stack reads carefully, so split the indexing into a
  // precomputation followed by a bounds check
  uint64_t stack_idx = addr - sp_start;
  if (stack_idx > addr) {
    LG_WRN("Stack miscalulation: %lu - %lu != %lu", addr, sp_start, stack_idx);
    return false;
  }
  *result = *(Dwarf_Word *)(us->stack + stack_idx);
  return true;
}

static DsoMod update_mod(DsoHdr *dso_hdr, Dwfl *dwfl, int pid, uint64_t pc) {
  if (!dwfl)
    return DsoMod(dso_hdr->find_res_not_found());

  // Lookup DSO
  DsoMod dso_mod_res(dso_hdr->dso_find_or_backpopulate(pid, pc));
  const DsoFindRes &dso_find_res = dso_mod_res._dso_find_res;
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

  // Try again if either of the criteria above were false
  if (!dso_mod_res._dwfl_mod) {
    const char *dso_filepath = dso._filename.c_str();
    LG_DBG("Invalid mod (%s), PID %d (%s)", dso_filepath, pid, dwfl_errmsg(-1));
    if (dso._type == ddprof::dso::kStandard) {
      const char *dso_name = strrchr(dso_filepath, '/') + 1;
      dso_mod_res._dwfl_mod = dwfl_report_elf(dwfl, dso_name, dso_filepath, -1,
                                              dso._start - dso._pgoff, false);
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

int frame_cb(Dwfl_Frame *state, void *arg) {
  static unsigned frame_id = 0;
#ifdef DEBUG
  LG_NFO("Beging frame id = %d", ++frame_id);
#endif
  struct UnwindState *us = (UnwindState *)arg;

  // Before we potentially exit, record the fact that we're processing a frame
  ddprof_stats_add(STATS_UNWIND_FRAMES, 1, NULL);

  if (max_stack_depth_reached(us)) {
    LG_DBG("Max number of stacks reached (frame#%u)", frame_id);
    return DWARF_CB_ABORT;
  }

  Dwarf_Addr pc = 0;
  bool isactivation = false;

  // Query the frame state to get the PC.  We skip the expensive check for
  // activation frame because the underlying DSO (module) may not have been
  // cached yet (but we need the PC to generate/check such a cache
  if (!dwfl_frame_pc(state, &pc, NULL)) {
    LG_DBG("dwfl_frame_pc NULL (%s)(frame#%u)", dwfl_errmsg(-1), frame_id);
    ddprof_stats_add(STATS_UNWIND_ERRORS, 1, NULL);
    return DWARF_CB_ABORT;
  }
  DsoMod dso_mod = update_mod(us->dso_hdr, us->dwfl, us->pid, pc);
  Dwfl_Module *mod = dso_mod._dwfl_mod;

  if (!mod) {
    LG_DBG("Unable to retrieve the Dwfl_Module: %s (frame#%u)", dwfl_errmsg(-1),
           frame_id);
    goto ERROR_FRAME_ABORT;
  }

  if (!dwfl_frame_pc(state, &pc, &isactivation)) {
    LG_DBG("Failure to compute frame PC: %s (frame#%u)", dwfl_errmsg(-1),
           frame_id);
    goto ERROR_FRAME_ABORT;
  }
  if (!isactivation)
    --pc;
  // updating frame can call backpopulate which invalidates the dso pointer
  // --> refresh the iterator
  dso_mod._dso_find_res = us->dso_hdr->dso_find_closest(us->pid, pc);
  if (!dso_mod._dso_find_res.second) {
    // strange scenario (backpopulate removed this dso)
    LG_DBG("Unable to retrieve DSO after call to frame_pc (frame#%u)",
           frame_id);
    goto ERROR_FRAME_ABORT;
  }
  // Now we register
  if (!IsDDResOK(add_dwfl_frame(us, dso_mod, pc))) {
    goto ERROR_FRAME_ABORT;
  }
#ifdef DEBUG
  LG_NFO("Success frame id = %d", frame_id);
#endif
  return DWARF_CB_OK;
ERROR_FRAME_ABORT:
  ddprof_stats_add(STATS_UNWIND_ERRORS, 1, NULL);

  if (dso_mod._dso_find_res.second) {
    const Dso &dso = *dso_mod._dso_find_res.first;
    add_dso_frame(us, dso, pc);
  } else {
    add_common_frame(us, CommonSymbolLookup::LookupCases::unknown_dso);
  }
#ifdef DEBUG
  LG_NFO("Error frame id = %d", frame_id);
#endif
  return DWARF_CB_ABORT;
}

int tid_cb(Dwfl_Thread *thread, void *targ) {
  dwfl_thread_getframes(thread, frame_cb, targ);
  return DWARF_CB_OK;
}

void unwind_dwfl_init(struct UnwindState *us) { us->dwfl_hdr = new DwflHdr(); }

void unwind_dwfl_free(struct UnwindState *us) {
  delete us->dwfl_hdr;
  us->dwfl_hdr = nullptr;
}

static DDRes unwind_attach(DwflWrapper &dwfl_wrapper, struct UnwindState *us) {
  static const Dwfl_Thread_Callbacks dwfl_callbacks = {
      .next_thread = next_thread,
      .get_thread = nullptr,
      .memory_read = memory_read,
      .set_initial_registers = set_initial_registers,
      .detach = nullptr,
      .thread_detach = nullptr,
  };
  return dwfl_wrapper.attach(us->pid, &dwfl_callbacks, us);
}

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

DDRes unwind_dwfl(UnwindState *us) {
  DDRes res;
  DwflWrapper &dwfl_wrapper = us->dwfl_hdr->get_or_insert(us->pid);
  us->dwfl = dwfl_wrapper._dwfl;
  // Update modules at the top
  DsoMod dso_mod = update_mod(us->dso_hdr, us->dwfl, us->pid, us->eip);

  if (dso_mod._dwfl_mod != NULL) {
    res = unwind_attach(dwfl_wrapper, us);
    if (!IsDDResOK(res)) { // frequent errors, avoid flooding logs
      LOG_ERROR_DETAILS(LG_DBG, res._what);
    }

    if (!dwfl_getthread_frames(us->dwfl, us->pid, frame_cb, us)) {
#ifdef DEBUG
      /* This should be investigated - when all errors are solved we can
       * reactivate the log (it is too verbose for now) */
      LG_DBG("[UNWIND] dwfl_getthread_frames was nonzero (%s)",
             dwfl_errmsg(-1));
#endif
      res = us->output.nb_locs > 0 ? ddres_init()
                                   : ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
    }
  } else {
    res = ddres_warn(DD_WHAT_UNHANDLED_DSO);
  }
  if (IsDDResNotOK(res)) {
    // analyse unwinding error
    if (!dso_mod._dso_find_res.second) {
      LG_DBG("Could not localize top-level IP: [%d](0x%lx)", us->pid, us->eip);
      analyze_unwinding_error(us->pid, us->eip);
      add_common_frame(us, CommonSymbolLookup::LookupCases::unknown_dso);
    } else {
      LG_DBG("Failed unwind: %s [%d](0x%lx)",
             dso_mod._dso_find_res.first->_filename.c_str(), us->pid, us->eip);
      us->dso_hdr->_stats.incr_metric(DsoStats::kUnwindFailure,
                                      dso_mod._dso_find_res.first->_type);
      add_dso_frame(us, *dso_mod._dso_find_res.first, us->eip);
    }
  }
  return res;
}
} // namespace ddprof
