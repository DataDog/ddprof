// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind_dwfl.hpp"

extern "C" {
#include "ddprof_stats.h"
#include "ddres.h"
#include "dwfl_internals.h"
#include "logger.h"

#include <dwarf.h>
}

#include "symbolize_dwfl.hpp"
#include "unwind_helpers.hpp"
#include "unwind_symbols.hpp"

extern "C" {
pid_t next_thread(Dwfl *, void *, void **);
bool set_initial_registers(Dwfl_Thread *, void *);
int frame_cb(Dwfl_Frame *, void *);
int tid_cb(Dwfl_Thread *, void *);
bool memory_read_dwfl(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result,
                      void *arg);
}

namespace ddprof {

/// memory_read as per prototype define in libdwfl
bool memory_read_dwfl(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result,
                      void *arg) {
  (void)dwfl;
  return memory_read(addr, result, arg);
}

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
  regs[6] = us->current_regs.ebp;
  regs[7] = us->current_regs.esp;
  regs[16] = us->current_regs.eip;

  return dwfl_thread_state_registers(thread, 0, 17, regs);
}

static void copy_current_registers(const Dwfl_Frame *state,
                                   UnwindRegisters &current_regs) {
  // Update regs on current unwinding position
  current_regs.ebp = state->regs[6];
  current_regs.esp = state->regs[7];
  current_regs.eip = state->regs[16];
}

// returns true if we should continue unwinding
static bool add_symbol(Dwfl_Frame *dwfl_frame, UnwindState *us) {

  if (max_stack_depth_reached(us)) {
    LG_DBG("Max number of stacks reached (depth#%lu)", us->output.nb_locs);
    return false;
  }

  Dwarf_Addr pc = 0;
  bool isactivation = false;

  // Query the frame state to get the PC.  We skip the expensive check for
  // activation frame because the underlying DSO (module) may not have been
  // cached yet (but we need the PC to generate/check such a cache
  if (!dwfl_frame_pc(dwfl_frame, &pc, NULL)) {
    LG_DBG("dwfl_frame_pc NULL (%s)(depth#%lu)", dwfl_errmsg(-1),
           us->output.nb_locs);
    ddprof_stats_add(STATS_UNWIND_ERRORS, 1, NULL);
    return true; // invalid pc : do not add frame
  }
  DsoMod dso_mod = update_mod(us->dso_hdr, us->dwfl, us->pid, pc);
  Dwfl_Module *mod = dso_mod._dwfl_mod;

  if (mod) {
    if (!dwfl_frame_pc(dwfl_frame, &pc, &isactivation)) {
      LG_DBG("Failure to compute frame PC: %s (depth#%lu)", dwfl_errmsg(-1),
             us->output.nb_locs);
      goto ERROR_FRAME_CONTINUE_UNWINDING;
    }
    if (!isactivation)
      --pc;
    // updating frame can call backpopulate which invalidates the dso pointer
    // --> refresh the iterator
    dso_mod._dso_find_res = us->dso_hdr->dso_find_closest(us->pid, pc);
    if (!dso_mod._dso_find_res.second) {
      // strange scenario (backpopulate removed this dso)
      LG_DBG("Unable to retrieve DSO after call to frame_pc (depth#%lu)",
             us->output.nb_locs);
      goto ERROR_FRAME_CONTINUE_UNWINDING;
    }
    // Now we register
    add_dwfl_frame(us, dso_mod, pc);
  } else {
    LG_DBG("Unable to retrieve the Dwfl_Module: %s (depth#%lu)",
           dwfl_errmsg(-1), us->output.nb_locs);
    goto ERROR_FRAME_CONTINUE_UNWINDING;
  }

  return true;
ERROR_FRAME_CONTINUE_UNWINDING:

  ddprof_stats_add(STATS_UNWIND_ERRORS, 1, NULL);
  if (dso_mod._dso_find_res.second) {
    const Dso &dso = *dso_mod._dso_find_res.first;
    add_dso_frame(us, dso, pc);
  } else {
    add_common_frame(us, CommonSymbolLookup::LookupCases::unknown_dso);
  }
  LG_DBG("Error frame (depth#%lu)", us->output.nb_locs);
  return true;
}

int frame_cb(Dwfl_Frame *dwfl_frame, void *arg) {
  UnwindState *us = (UnwindState *)arg;
#ifdef DEBUG
  LG_NFO("Beging depth %lu", us->output.nb_locs);
#endif

  copy_current_registers(dwfl_frame, us->current_regs);
#ifdef DEBUG
  LG_NTC("Current regs : ebp %lx, esp %lx, eip %lx", us->current_regs.ebp,
         us->current_regs.esp, us->current_regs.eip);
#endif

  // Before we potentially exit, record the fact that we're processing a frame
  ddprof_stats_add(STATS_UNWIND_FRAMES, 1, NULL);

  if (!add_symbol(dwfl_frame, us)) {
    return DWARF_CB_ABORT;
  }

  return DWARF_CB_OK;
}

int tid_cb(Dwfl_Thread *thread, void *targ) {
  dwfl_thread_getframes(thread, frame_cb, targ);
  return DWARF_CB_OK;
}

static DDRes unwind_attach(DwflWrapper &dwfl_wrapper, struct UnwindState *us) {
  static const Dwfl_Thread_Callbacks dwfl_callbacks = {
      .next_thread = next_thread,
      .get_thread = nullptr,
      .memory_read = memory_read_dwfl,
      .set_initial_registers = set_initial_registers,
      .detach = nullptr,
      .thread_detach = nullptr,
  };
  return dwfl_wrapper.attach(us->pid, &dwfl_callbacks, us);
}

DDRes unwind_dwfl(UnwindState *us, DwflWrapper &dwfl_wrapper) {
  DDRes res;
  res = unwind_attach(dwfl_wrapper, us);
  if (!IsDDResOK(res)) { // frequent errors, avoid flooding logs
    LOG_ERROR_DETAILS(LG_DBG, res._what);
    return res;
  }
  //
  // Launch the dwarf unwinding (uses frame_cb callback)
  if (!dwfl_getthread_frames(us->dwfl, us->pid, frame_cb, us)) {
    res = us->output.nb_locs > 0 ? ddres_init()
                                 : ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
  }
  return res;
}

} // namespace ddprof
