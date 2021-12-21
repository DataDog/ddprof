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
}

#include "dwfl_thread_callbacks.hpp"
#include "symbol_hdr.hpp"
#include "unwind_helpers.hpp"
#include "unwind_state.hpp"

extern "C" {
int frame_cb(Dwfl_Frame *, void *);
}

namespace ddprof {

DDRes unwind_init_dwfl(UnwindState *us) {
  // Create or get the dwfl object associated to cache
  us->_dwfl_wrapper = &(us->dwfl_hdr.get_or_insert(us->pid));
  if (!us->_dwfl_wrapper->_attached) {
    // we need to add at least one module to figure out the architecture (to
    // create the unwinding backend)

    DsoHdr::DsoMap &map = us->dso_hdr._map[us->pid];
    if (map.empty()) {
      int nb_elts;
      us->dso_hdr.pid_backpopulate(us->pid, nb_elts);
    }

    bool success = false;
    // Find an elf file we can load for this PID
    for (auto el : map) {
      if (el.second._executable) {
        const Dso &dso = el.second;
        FileInfoId_t file_info_id = us->dso_hdr.get_or_insert_file_info(dso);
        if (file_info_id <= k_file_info_error) {
          LG_DBG("Unable to find file for DSO %s", dso.to_string().c_str());
          continue;
        }
        const FileInfoValue &file_info_value =
            us->dso_hdr.get_file_info_value(file_info_id);
        if (IsDDResOK(us->_dwfl_wrapper->register_mod(us->current_regs.eip, dso,
                                                      file_info_value))) {
          // one success is fine
          success = true;
          break;
        }
      }
    }
    if (!success) {
      LG_DBG("Unable to attach a mod for PID%d", us->pid);
      return ddres_warn(DD_WHAT_UW_ERROR);
    }

    static const Dwfl_Thread_Callbacks dwfl_callbacks = {
        .next_thread = next_thread,
        .get_thread = nullptr,
        .memory_read = memory_read_dwfl,
        .set_initial_registers = set_initial_registers,
        .detach = nullptr,
        .thread_detach = nullptr,
    };
    // Creates the dwfl unwinding backend
    return us->_dwfl_wrapper->attach(us->pid, &dwfl_callbacks, us);
  }
  return ddres_init();
}

static void trace_unwinding_end(UnwindState *us) {
  if (LL_DEBUG <= LOG_getlevel()) {
    DsoHdr::DsoFindRes find_res =
        us->dso_hdr.dso_find_closest(us->pid, us->current_regs.eip);
    if (find_res.second) {
      LG_DBG("Stopped at %lx - dso %s - error %s", us->current_regs.eip,
             find_res.first->second.to_string().c_str(), dwfl_errmsg(-1));
    } else {
      LG_DBG("Unknown DSO %lx - error %s", us->current_regs.eip,
             dwfl_errmsg(-1));
    }
  }
}

static void add_dwfl_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc);

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

  if (!dwfl_frame_pc(dwfl_frame, &pc, &isactivation)) {
    LG_DBG("Failure to compute frame PC: %s (depth#%lu)", dwfl_errmsg(-1),
           us->output.nb_locs);
    add_error_frame(nullptr, us, pc, SymbolErrors::dwfl_frame);
    return true; // invalid pc : do not add frame
  }

  if (!isactivation)
    --pc;

  DsoHdr::DsoFindRes find_res =
      us->dso_hdr.dso_find_or_backpopulate(us->pid, pc);
  if (!find_res.second) {
    // no matching file was found
    LG_DBG("[UW]%d: DSO not found at 0x%lx (depth#%lu)", us->pid, pc,
           us->output.nb_locs);
    add_error_frame(nullptr, us, pc, SymbolErrors::unknown_dso);
    return true;
  }

  // Now we register
  add_dwfl_frame(us, find_res.first->second, pc);
  return true;
}

// frame_cb callback at every frame for the dwarf unwinding
static int frame_cb(Dwfl_Frame *dwfl_frame, void *arg) {
  UnwindState *us = (UnwindState *)arg;
#ifdef DEBUG
  LG_NFO("Beging depth %lu", us->output.nb_locs);
#endif

  // Before we potentially exit, record the fact that we're processing a frame
  ddprof_stats_add(STATS_UNWIND_FRAMES, 1, NULL);

  if (!add_symbol(dwfl_frame, us)) {
    return DWARF_CB_ABORT;
  }

  copy_current_registers(dwfl_frame, us->current_regs);
#ifdef DEBUG
  LG_NTC("Current regs : ebp %lx, esp %lx, eip %lx", us->current_regs.ebp,
         us->current_regs.esp, us->current_regs.eip);
#endif

  return DWARF_CB_OK;
}

DDRes unwind_dwfl(UnwindState *us) {
  DDRes res = unwind_init_dwfl(us);
  if (!IsDDResOK(res)) {
    LOG_ERROR_DETAILS(LG_DBG, res._what);
    return res;
  }
  //
  // Launch the dwarf unwinding (uses frame_cb callback)
  if (dwfl_getthread_frames(us->_dwfl_wrapper->_dwfl, us->pid, frame_cb, us) !=
      0) {
    trace_unwinding_end(us);
  }
  res = us->output.nb_locs > 0 ? ddres_init()
                               : ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
  return res;
}

void add_dwfl_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc) {

  SymbolHdr &unwind_symbol_hdr = us->symbol_hdr;
  // if not encountered previously, update file location / key
  FileInfoId_t file_info_id = us->dso_hdr.get_or_insert_file_info(dso);
  if (file_info_id <= k_file_info_error) {
    // unable to acces file: add as much info from dso
    add_dso_frame(us, dso, pc);
    return;
  }

  const FileInfoValue &file_info_value =
      us->dso_hdr.get_file_info_value(file_info_id);

  // ensure unwinding backend has access to this module
  us->_dwfl_wrapper->register_mod(pc, dso, file_info_value);

  UnwindOutput *output = &us->output;
  int64_t current_loc_idx = output->nb_locs;

  output->locs[current_loc_idx]._symbol_idx =
      unwind_symbol_hdr._dwfl_symbol_lookup_v2.get_or_insert(
          us->_dwfl_wrapper->_dwfl, unwind_symbol_hdr._symbol_table,
          unwind_symbol_hdr._dso_symbol_lookup, pc, dso, file_info_value);
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
}

} // namespace ddprof
