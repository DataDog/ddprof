extern "C" {
#include "unwind.h"

#include "ddres.h"
#include "demangle.h"
#include "dso.h"
#include "logger.h"
#include "signal_helper.h"
#include "unwind_symbols.h"
}

#include "dso.hpp"
#include "unwind_symbols.hpp"

#define UNUSED(x) (void)(x)

using ddprof::Dso;
using ddprof::DsoSetConstIt;
using ddprof::DsoSetIt;
using ddprof::DsoStats;

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

static DDRes add_frame(UnwindState *us, DsoMod dso_mod, uint64_t pc) {
  Dwfl_Module *mod = dso_mod._dwfl_mod;
  // if we are here, we can assume we found a dso
  DsoUID_t dso_id = dso_mod._dso_find_res.first->_id;

  UnwindOutput *output = &us->output;
  int64_t current_idx = output->nb_locs;

  DDRes cache_status = ipinfo_lookup_get(
      us->symbols_hdr, mod, pc, dso_id, &output->locs[current_idx]._ipinfo_idx);
  if (IsDDResNotOK(cache_status)) {
    LG_DBG("Error from dwflmod_cache_status");
    return cache_status;
  }

  output->locs[current_idx].ip = pc;

  ddprof::mapinfo_lookup_get(us->symbols_hdr->_mapinfo_lookup,
                             us->symbols_hdr->_mapinfo_table, mod, dso_id,
                             &(output->locs[current_idx]._map_info_idx));

  output->nb_locs++;
  return ddres_init();
}

/// memory_read as per prototype define in libdwfl
static bool memory_read(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result,
                        void *arg) {
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
  struct UnwindState *us = (UnwindState *)arg;
  Dwarf_Addr pc = 0;
  bool isactivation = false;

  // Before we potentially exit, record the fact that we're processing a frame
  ddprof_stats_add(STATS_UNWIND_FRAMES, 1, NULL);

  // Query the frame state to get the PC.  We skip the expensive check for
  // activation frame because the underlying DSO (module) may not have been
  // cached yet (but we need the PC to generate/check such a cache
  if (!dwfl_frame_pc(state, &pc, NULL)) {
    LG_DBG("dwfl_frame_pc NULL (%s)", dwfl_errmsg(-1));
    ddprof_stats_add(STATS_UNWIND_ERRORS, 1, NULL);
    return DWARF_CB_ABORT;
  }
  DsoMod dso_mod = update_mod(us->dso_hdr, us->dwfl, us->pid, pc);
  Dwfl_Module *mod = dso_mod._dwfl_mod;

  if (!mod) {
    LG_DBG("Unable to retrieve the Dwfl_Module");
    ddprof_stats_add(STATS_UNWIND_ERRORS, 1, NULL);
    return DWARF_CB_ABORT;
  }

  if (!dwfl_frame_pc(state, &pc, &isactivation)) {
    LG_DBG("Failure to compute frame PC: %s", dwfl_errmsg(-1));
    ddprof_stats_add(STATS_UNWIND_ERRORS, 1, NULL);
    return DWARF_CB_ABORT;
  }
  if (!isactivation)
    --pc;

  Dwfl_Thread *thread = dwfl_frame_thread(state);
  if (!thread) {
    LG_DBG("dwfl_frame_thread was zero: (%s)", dwfl_errmsg(-1));
    ddprof_stats_add(STATS_UNWIND_ERRORS, 1, NULL);
    return DWARF_CB_ABORT;
  }
  Dwfl *dwfl = dwfl_thread_dwfl(thread);
  if (!dwfl) {
    LG_DBG("dwfl_thread_dwfl was zero: (%s)", dwfl_errmsg(-1));
    ddprof_stats_add(STATS_UNWIND_ERRORS, 1, NULL);
    return DWARF_CB_ABORT;
  }

  // Now we register
  if (!IsDDResOK(add_frame(us, dso_mod, pc))) {
    ddprof_stats_add(STATS_UNWIND_ERRORS, 1, NULL);
    return DWARF_CB_ABORT;
  }

  return DWARF_CB_OK;
}

int tid_cb(Dwfl_Thread *thread, void *targ) {
  dwfl_thread_getframes(thread, frame_cb, targ);
  return DWARF_CB_OK;
}

static DDRes unwind_dwfl_begin(struct UnwindState *us) {
  static char *debuginfo_path;
  static const Dwfl_Callbacks proc_callbacks = {
      .find_elf = dwfl_linux_proc_find_elf,
      .find_debuginfo = dwfl_standard_find_debuginfo,
      .section_address = nullptr,
      .debuginfo_path = &debuginfo_path,
  };
  us->dwfl = dwfl_begin(&proc_callbacks);
  if (!us->dwfl) {
    LG_WRN("dwfl_begin was zero (%s)", dwfl_errmsg(-1));
    return ddres_error(DD_WHAT_DWFL_LIB_ERROR);
  }
  us->attached = false;

  return ddres_init();
}

static void unwind_dwfl_end(struct UnwindState *us) {
  if (us->dwfl) {
    dwfl_end(us->dwfl);
    us->dwfl = 0;
  }
  us->attached = false;
}

static DDRes unwind_attach(struct UnwindState *us) {
  static const Dwfl_Thread_Callbacks dwfl_callbacks = {
      .next_thread = next_thread,
      .get_thread = nullptr,
      .memory_read = memory_read,
      .set_initial_registers = set_initial_registers,
      .detach = nullptr,
      .thread_detach = nullptr,
  };

  if (us->attached) {
    return ddres_init();
  }
  if (dwfl_attach_state(us->dwfl, NULL, us->pid, &dwfl_callbacks, us)) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_DWFL_LIB_ERROR,
                           "[UNWIND] Error while calling dwfl_attach_state");
  }
  us->attached = true;
  return ddres_init();
}

DDRes unwind_init(struct UnwindState *us) {
  DDRES_CHECK_FWD(unwind_symbols_hdr_init(&(us->symbols_hdr)));
  DDRES_CHECK_FWD(libdso_init(&us->dso_hdr));
  elf_version(EV_CURRENT);
  DDRES_CHECK_FWD(unwind_dwfl_begin(us));
  return ddres_init();
}

void unwind_free(struct UnwindState *us) {
  unwind_symbols_hdr_free(us->symbols_hdr);
  libdso_free(us->dso_hdr);
  unwind_dwfl_end(us);
  us->symbols_hdr = NULL;
}

DDRes unwindstate__unwind(struct UnwindState *us) {
  DDRes res;
  // Update modules at the top
  DsoMod dso_mod = update_mod(us->dso_hdr, us->dwfl, us->pid, us->eip);

  if (dso_mod._dwfl_mod != NULL) {
    res = unwind_attach(us);
    if (!IsDDResOK(res)) { // frequent errors, avoid flooding logs
      LOG_ERROR_DETAILS(LG_DBG, res._what);
    }

    if (!dwfl_getthread_frames(us->dwfl, us->pid, frame_cb, us)) {
      /* This should be investigated - when all errors are solved we can
       * reactivate the log (it is too verbose for now) */
      // LG_DBG("[UNWIND] dwfl_getthread_frames was nonzero (%s)",
      // dwfl_errmsg(-1));
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
    } else {
      LG_DBG("Failed unwind: %s [%d](0x%lx)",
             dso_mod._dso_find_res.first->_filename.c_str(), us->pid, us->eip);

      us->dso_hdr->_stats.incr_metric(DsoStats::kUnwindFailure,
                                      dso_mod._dso_find_res.first->_type);
    }
  }
  return res;
}

void analyze_unwinding_error(pid_t pid, uint64_t eip) {
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
