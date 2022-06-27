// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind_dwfl.hpp"

#include "ddprof_stats.hpp"
#include "ddres.hpp"
#include "dwfl_internals.hpp"
#include "dwfl_thread_callbacks.hpp"
#include "logger.hpp"
#include "symbol_hdr.hpp"
#include "unwind_helpers.hpp"
#include "unwind_state.hpp"

int frame_cb(Dwfl_Frame *, void *);

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
    for (auto it = map.begin(); it != map.end(); ++it) {
      Dso &dso = it->second;
      if (dso._executable) {
        FileInfoId_t file_info_id = us->dso_hdr.get_or_insert_file_info(dso);
        if (file_info_id <= k_file_info_error) {
          LG_DBG("Unable to find file for DSO %s", dso.to_string().c_str());
          continue;
        }
        const FileInfoValue &file_info_value =
            us->dso_hdr.get_file_info_value(file_info_id);

        // get low and high addr for this module
        DDProfModRange mod_range = us->dso_hdr.find_mod_range(it, map);
        DDProfMod *ddprof_mod = us->_dwfl_wrapper->register_mod(us->current_ip, dso, mod_range, file_info_value);
        if (ddprof_mod->_mod) {
          // one success is fine
          success = true;
          break;
        }
        else if (ddprof_mod->_status == DDProfMod::kInconsistent) {
          return ddres_warn(DD_WHAT_UW_ERROR);
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
        us->dso_hdr.dso_find_closest(us->pid, us->current_ip);
    if (find_res.second) {
      LG_DBG("Stopped at %lx - dso %s - error %s", us->current_ip,
             find_res.first->second.to_string().c_str(), dwfl_errmsg(-1));
    } else {
      LG_DBG("Unknown DSO %lx - error %s", us->current_ip, dwfl_errmsg(-1));
    }
  }
}
static DDRes add_dwfl_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc, DDProfMod *ddprof_mod, const FileInfoValue &file_info_value);

// returns true if we should continue unwinding
static DDRes add_symbol(Dwfl_Frame *dwfl_frame, UnwindState *us) {

  if (max_stack_depth_reached(us)) {
    LG_DBG("Max stack depth reached (depth#%lu)", us->output.nb_locs);
    ddprof_stats_add(STATS_UNWIND_TRUNCATED_OUTPUT, 1, nullptr);
    return ddres_warn(DD_WHAT_UW_MAX_DEPTH);
  }

  Dwarf_Addr pc = 0;
  bool isactivation = false;

  if (!dwfl_frame_pc(dwfl_frame, &pc, &isactivation)) {
    LG_DBG("Failure to compute frame PC: %s (depth#%lu)", dwfl_errmsg(-1),
           us->output.nb_locs);
    add_error_frame(nullptr, us, pc, SymbolErrors::dwfl_frame);
    return ddres_init(); // invalid pc : do not add frame
  }

  if (!isactivation)
    --pc;

  us->current_ip = pc;

  DsoHdr::DsoFindRes find_res =
      us->dso_hdr.dso_find_or_backpopulate(us->pid, pc);
  if (!find_res.second) {
    // no matching file was found
    LG_DBG("[UW]%d: DSO not found at 0x%lx (depth#%lu)", us->pid, pc,
           us->output.nb_locs);
    add_error_frame(nullptr, us, pc, SymbolErrors::unknown_dso);
    return ddres_init();
  }
  const Dso &dso = find_res.first->second;
    // if not encountered previously, update file location / key
  FileInfoId_t file_info_id = us->dso_hdr.get_or_insert_file_info(dso);
  if (file_info_id <= k_file_info_error) {
    // unable to acces file: add available info from dso
    add_dso_frame(us, dso, pc);
    // We could stop here or attempt to continue in the dwarf unwinding
    // sometimes frame pointer lets us go further -> So we continue
    return ddres_init();
  }

  const FileInfoValue &file_info_value =
      us->dso_hdr.get_file_info_value(file_info_id);

  DDProfModRange mod_range = us->dso_hdr.find_mod_range(find_res.first, us->dso_hdr._map[us->pid]);
  // ensure unwinding backend has access to this module (and check consistency)
  DDProfMod *ddprof_mod = us->_dwfl_wrapper->register_mod(pc, dso, mod_range, file_info_value);
  // Updates in DSO layout can create inconsistencies
  if (ddprof_mod->_status == DDProfMod::kInconsistent ) {
    return ddres_warn(DD_WHAT_UW_ERROR);
  }

  // Now we register
  if (IsDDResNotOK(add_dwfl_frame(us, dso, pc, ddprof_mod, file_info_value))) {
    return ddres_warn(DD_WHAT_UW_ERROR);
  }
  return ddres_init();
}

// frame_cb callback at every frame for the dwarf unwinding
static int frame_cb(Dwfl_Frame *dwfl_frame, void *arg) {
  UnwindState *us = (UnwindState *)arg;
#ifdef DEBUG
  LG_NFO("Beging depth %lu", us->output.nb_locs);
#endif

  // Before we potentially exit, record the fact that we're processing a frame
  ddprof_stats_add(STATS_UNWIND_FRAMES, 1, NULL);

  if (IsDDResNotOK(add_symbol(dwfl_frame, us))) {
    return DWARF_CB_ABORT;
  }

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

static DDRes add_dwfl_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc, DDProfMod *ddprof_mod, const FileInfoValue &file_info_value) {

  SymbolHdr &unwind_symbol_hdr = us->symbol_hdr;

  UnwindOutput *output = &us->output;
  int64_t current_loc_idx = output->nb_locs;

  // get or create the dwfl symbol
  output->locs[current_loc_idx]._symbol_idx =
      unwind_symbol_hdr._dwfl_symbol_lookup_v2.get_or_insert(
          *ddprof_mod, unwind_symbol_hdr._symbol_table, unwind_symbol_hdr._dso_symbol_lookup, pc, dso, file_info_value);
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

  return ddres_init();
}

} // namespace ddprof
