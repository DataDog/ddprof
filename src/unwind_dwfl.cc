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
#include "runtime_symbol_lookup.hpp"
#include "symbol_hdr.hpp"
#include "unwind_helpers.hpp"
#include "unwind_state.hpp"

namespace ddprof {

namespace {

int frame_cb(Dwfl_Frame * /*dwfl_frame*/, void * /*arg*/);

DDRes add_dwfl_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc,
                     const DDProfMod &ddprof_mod, FileInfoId_t file_info_id);

void trace_unwinding_end(UnwindState *us) {
  if (LL_DEBUG <= LOG_getlevel()) {
    DsoHdr::DsoFindRes const find_res =
        us->dso_hdr.dso_find_closest(us->pid, us->current_ip);
    SymbolIdx_t const symIdx =
        us->output.locs[us->output.locs.size() - 1]._symbol_idx;
    if (find_res.second) {
      const std::string &last_func =
          us->symbol_hdr._symbol_table[symIdx]._symname;
      LG_DBG("Stopped at %lx - dso %s - error %s (%s)", us->current_ip,
             find_res.first->second.to_string().c_str(), dwfl_errmsg(-1),
             last_func.c_str());
    } else {
      LG_DBG("Unknown DSO %lx - error %s", us->current_ip, dwfl_errmsg(-1));
    }
  }
}

bool is_infinite_loop(UnwindState *us) {
  UnwindOutput &output = us->output;
  uint64_t const nb_locs = output.locs.size();
  unsigned const nb_frames_to_check = 3;
  if (nb_locs <= nb_frames_to_check) {
    return false;
  }
  for (unsigned i = 1; i < nb_frames_to_check; ++i) {
    FunLoc const &n_minus_one_loc = output.locs[nb_locs - i];
    FunLoc const &n_minus_two_loc = output.locs[nb_locs - i - 1];
    if (n_minus_one_loc.ip != n_minus_two_loc.ip) {
      return false;
    }
  }
  return true;
}

// check for runtime symbols provided in /tmp files
DDRes add_runtime_symbol_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc,
                               std::string_view jitdump_path);

// returns an OK status if we should continue unwinding
DDRes add_symbol(Dwfl_Frame *dwfl_frame, UnwindState *us) {
  if (is_max_stack_depth_reached(*us)) {
    add_common_frame(us, SymbolErrors::truncated_stack);
    LG_DBG("Max stack depth reached (depth#%lu)", us->output.locs.size());
    ddprof_stats_add(STATS_UNWIND_TRUNCATED_OUTPUT, 1, nullptr);
    return ddres_warn(DD_WHAT_UW_MAX_DEPTH);
  }

  Dwarf_Addr pc = 0;
  if (!dwfl_frame_pc(dwfl_frame, &pc, nullptr)) {
    LG_DBG("Failure to compute frame PC: %s (depth#%lu)", dwfl_errmsg(-1),
           us->output.locs.size());
    add_error_frame(nullptr, us, pc, SymbolErrors::dwfl_frame);
    return {}; // invalid pc : do not add frame
  }
  us->current_ip = pc;
  DsoHdr &dsoHdr = us->dso_hdr;
  DsoHdr::PidMapping &pid_mapping = dsoHdr.get_pid_mapping(us->pid);
  if (!pc) {
    // Unwinding can end on a null address
    // Example: alpine 3.17
    return {};
  }

  // When LOAD segments are ambiguous, do a backpopulate and a second attempt at
  // registering module
  bool retry = false;
  DsoHdr::DsoFindRes find_res;
  DDProfMod *ddprof_mod = nullptr;
  FileInfoId_t file_info_id;
  do {
    find_res = dsoHdr.dso_find_or_backpopulate(pid_mapping, us->pid, pc);
    if (!find_res.second) {
      // no matching file was found
      LG_DBG("[UW] (PID%d) DSO not found at 0x%lx (depth#%lu)", us->pid, pc,
             us->output.locs.size());
      add_error_frame(nullptr, us, pc, SymbolErrors::unknown_dso);
      return {};
    }
    const Dso &dso = find_res.first->second;
    std::string_view jitdump_path = {};
    if (has_runtime_symbols(dso._type)) {
      if (pid_mapping._jitdump_addr) {
        DsoHdr::DsoFindRes const find_mapping = DsoHdr::dso_find_closest(
            pid_mapping._map, pid_mapping._jitdump_addr);
        if (find_mapping.second) { // jitdump exists
          jitdump_path = find_mapping.first->second._filename;
        }
      }
      return add_runtime_symbol_frame(us, dso, pc, jitdump_path);
    }
    // if not encountered previously, update file location / key
    file_info_id = us->dso_hdr.get_or_insert_file_info(dso);
    if (file_info_id <= k_file_info_error) {
      // unable to access file: add available info from dso
      add_dso_frame(us, dso, pc, "pc");
      // We could stop here or attempt to continue in the dwarf unwinding
      // sometimes frame pointer lets us go further -> So we continue
      return {};
    }
    const FileInfoValue &file_info_value =
        us->dso_hdr.get_file_info_value(file_info_id);
    ddprof_mod = us->_dwfl_wrapper->unsafe_get(file_info_id);
    if (!ddprof_mod) {
      auto dsoRange = DsoHdr::get_elf_range(pid_mapping._map, find_res.first);
      // ensure unwinding backend has access to this module (and check
      // consistency)
      auto res = us->_dwfl_wrapper->register_mod(pc, dsoRange, file_info_value,
                                                 &ddprof_mod);

      if (!IsDDResOK(res)) {
        int nb_elts_added = 0;
        if (!retry && res._what == DD_WHAT_AMBIGUOUS_LOAD_SEGMENT &&
            dsoHdr.pid_backpopulate(us->pid, nb_elts_added) &&
            nb_elts_added > 0) {
          // ambiguous LOAD segments detected, retry after backpopulate
          retry = true;
          // clear errored state to allow retry
          file_info_value.reset_errored();
        } else {
          return ddres_warn(DD_WHAT_UW_ERROR);
        }
      }
    }
  } while (retry);

  const Dso &dso = find_res.first->second;

  // To check that we are in an activation frame, we unwind the current frame
  // This means we need access to the module information.
  // Now that we have loaded the module, we can check if we are an activation
  // frame
  bool is_activation = false;

  if (!dwfl_frame_pc(dwfl_frame, &pc, &is_activation)) {
    LG_DBG("Failure to compute frame PC: %s (depth#%lu)", dwfl_errmsg(-1),
           us->output.locs.size());
    add_error_frame(nullptr, us, pc, SymbolErrors::dwfl_frame);
    return {}; // invalid pc : do not add frame
  }
  if (!is_activation) {
    --pc;
  }
  us->current_ip = pc;

  // Now we register
  if (IsDDResNotOK(add_dwfl_frame(us, dso, pc, *ddprof_mod, file_info_id))) {
    return ddres_warn(DD_WHAT_UW_ERROR);
  }
  return {};
}

// frame_cb callback at every frame for the dwarf unwinding
int frame_cb(Dwfl_Frame *dwfl_frame, void *arg) {
  auto *us = static_cast<UnwindState *>(arg);
#ifdef DEBUG
  LG_NFO("Begin depth %lu", us->output.locs.size());
#endif
  int const dwfl_error_value = dwfl_errno();
  if (dwfl_error_value) {
    // Check if dwarf unwinding was a failure we can get stuck in infinite loops
    if (is_infinite_loop(us)) {
      LG_DBG("Break out of unwinding (possible infinite loop)");
      return DWARF_CB_ABORT;
    }
  }
#ifdef DEBUG
  // We often fallback to frame pointer unwinding (which logs an error)
  if (dwfl_error_value) {
    LG_DBG("Error flagged at depth = %lu -- %d Error:%s ",
           us->output.locs.size(), dwfl_error_value,
           dwfl_errmsg(dwfl_error_value));
  }
#endif
  // Before we potentially exit, record the fact that we're processing a frame
  ddprof_stats_add(STATS_UNWIND_FRAMES, 1, nullptr);

  if (IsDDResNotOK(add_symbol(dwfl_frame, us))) {
    return DWARF_CB_ABORT;
  }

  return DWARF_CB_OK;
}

DDRes add_dwfl_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc,
                     const DDProfMod &ddprof_mod, FileInfoId_t file_info_id) {
  SymbolHdr &unwind_symbol_hdr = us->symbol_hdr;
  // get or create the dwfl symbol
  SymbolIdx_t const symbol_idx =
      unwind_symbol_hdr._dwfl_symbol_lookup.get_or_insert(
          us->_dwfl_wrapper->_dwfl, ddprof_mod, unwind_symbol_hdr._symbol_table,
          unwind_symbol_hdr._dso_symbol_lookup, file_info_id, pc, dso);
  MapInfoIdx_t const map_idx = us->symbol_hdr._mapinfo_lookup.get_or_insert(
      us->pid, us->symbol_hdr._mapinfo_table, dso, ddprof_mod._build_id);
  return add_frame(symbol_idx, map_idx, pc, us);
}

// check for runtime symbols provided in /tmp files
DDRes add_runtime_symbol_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc,
                               std::string_view jitdump_path) {
  SymbolHdr &unwind_symbol_hdr = us->symbol_hdr;
  SymbolTable &symbol_table = unwind_symbol_hdr._symbol_table;
  RuntimeSymbolLookup &runtime_symbol_lookup =
      unwind_symbol_hdr._runtime_symbol_lookup;
  SymbolIdx_t symbol_idx = -1;
  if (jitdump_path.empty()) {
    symbol_idx =
        runtime_symbol_lookup.get_or_insert(dso._pid, pc, symbol_table);
  } else {
    symbol_idx = runtime_symbol_lookup.get_or_insert_jitdump(
        dso._pid, pc, symbol_table, jitdump_path);
  }
  if (symbol_idx == -1) {
    add_dso_frame(us, dso, pc, "pc");
    return {};
  }

  MapInfoIdx_t const map_idx = us->symbol_hdr._mapinfo_lookup.get_or_insert(
      us->pid, us->symbol_hdr._mapinfo_table, dso, {});

  return add_frame(symbol_idx, map_idx, pc, us);
}
} // namespace

DDRes unwind_init_dwfl(UnwindState *us) {
  // Create or get the dwfl object associated to cache
  us->_dwfl_wrapper = &(us->dwfl_hdr.get_or_insert(us->pid));
  if (!us->_dwfl_wrapper->_attached) {
    // we need to add at least one module to figure out the architecture (to
    // create the unwinding backend)

    DsoHdr::DsoMap const &map = us->dso_hdr.get_pid_mapping(us->pid)._map;
    if (map.empty()) {
      int nb_elts;
      us->dso_hdr.pid_backpopulate(us->pid, nb_elts);
    }

    bool success = false;
    // Find an elf file we can load for this PID
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
      const Dso &dso = it->second;
      if (dso.is_executable()) {
        FileInfoId_t const file_info_id =
            us->dso_hdr.get_or_insert_file_info(dso);
        if (file_info_id <= k_file_info_error) {
          LG_DBG("Unable to find file for DSO %s", dso.to_string().c_str());
          continue;
        }
        const FileInfoValue &file_info_value =
            us->dso_hdr.get_file_info_value(file_info_id);

        DDProfMod *ddprof_mod = nullptr;
        auto res = us->_dwfl_wrapper->register_mod(
            us->current_ip, DsoHdr::get_elf_range(map, it), file_info_value,
            &ddprof_mod);
        if (IsDDResOK(res)) {
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
  return {};
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
  res = !us->output.locs.empty() ? ddres_init()
                                 : ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
  return res;
}

} // namespace ddprof
