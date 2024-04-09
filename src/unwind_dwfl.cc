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
#include "unique_fd.hpp"
#include "unwind_helper.hpp"
#include "unwind_state.hpp"

#include <fcntl.h>

namespace ddprof {

namespace {

int frame_cb(Dwfl_Frame * /*dwfl_frame*/, void * /*arg*/);

DDRes add_unsymbolized_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc,
                             const DDProfMod &ddprof_mod,
                             FileInfoId_t file_info_id);

void trace_unwinding_end(UnwindState *us) {
  if (LL_DEBUG <= LOG_getlevel()) {
    DsoHdr::DsoFindRes const find_res =
        us->dso_hdr.dso_find_closest(us->pid, us->current_ip);
    if (find_res.second && !us->output.locs.empty()) {
      LG_DBG("Stopped at %lx - dso %s - error %s (0x%lx)", us->current_ip,
             find_res.first->second.to_string().c_str(), dwfl_errmsg(-1),
             us->output.locs[us->output.locs.size() - 1].elf_addr);
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
    add_error_frame(nullptr, us, pc, SymbolErrors::unwind_failure);
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

  DsoHdr::DsoFindRes find_res;
  DDProfMod *ddprof_mod = nullptr;
  FileInfoId_t file_info_id;
  for (int attempt = 0; attempt < 2; ++attempt) {
    find_res = dsoHdr.dso_find_or_backpopulate(pid_mapping, us->pid, pc);
    if (!find_res.second) {
      // no matching file was found
      LG_DBG("[UW] (PID%d) DSO not found at 0x%lx (depth#%lu)", us->pid, pc,
             us->output.locs.size());
      add_error_frame(nullptr, us, pc, SymbolErrors::unknown_mapping);
      return {};
    }
    const Dso &dso = find_res.first->second;
    std::string_view jitdump_path = {};
    if (has_runtime_symbols(dso)) {
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
    if (ddprof_mod) {
      break;
    }

    // ensure unwinding backend has access to this module (and check
    // consistency)
    auto res = us->_dwfl_wrapper->register_mod(pc, find_res.first->second,
                                               file_info_value, &ddprof_mod);
    if (IsDDResOK(res)) {
      break;
    }
    int nb_elts_added = 0;
    if (attempt == 0 && dsoHdr.pid_backpopulate(us->pid, nb_elts_added) &&
        nb_elts_added > 0) {
      // retry after backpopulate
      // clear errored state to allow retry
      file_info_value.reset_errored();
    } else {
      break;
    }
  }

  if (ddprof_mod == nullptr) {
    // unable to register module
    return ddres_warn(DD_WHAT_UW_ERROR);
  }

  const Dso &dso = find_res.first->second;

  // To check that we are in an activation frame, we unwind the current frame
  // This means we need access to the module information.
  // Now that we have loaded the module, we can check if we are an activation
  // frame
  bool is_activation = false;

  if (!dwfl_frame_pc(dwfl_frame, &pc, &is_activation)) {
    LG_DBG("Failure to compute frame PC: %s (depth#%lu)", dwfl_errmsg(-1),
           us->output.locs.size());
    add_error_frame(nullptr, us, pc, SymbolErrors::unwind_failure);
    return {}; // invalid pc : do not add frame
  }
  if (!is_activation) {
    --pc;
  }
  us->current_ip = pc;

  // Now we register
  if (IsDDResNotOK(
          add_unsymbolized_frame(us, dso, pc, *ddprof_mod, file_info_id))) {
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

DDRes add_unsymbolized_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc,
                             const DDProfMod &ddprof_mod,
                             FileInfoId_t file_info_id) {
  MapInfoIdx_t const map_idx = us->symbol_hdr._mapinfo_lookup.get_or_insert(
      us->pid, us->symbol_hdr._mapinfo_table, dso, ddprof_mod._build_id);
  return add_frame(k_symbol_idx_null, file_info_id, map_idx, pc,
                   pc - ddprof_mod._sym_bias, us);
}

// check for runtime symbols provided in /tmp files
DDRes add_runtime_symbol_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc,
                               std::string_view jitdump_path) {
  SymbolHdr &unwind_symbol_hdr = us->symbol_hdr;
  SymbolTable &symbol_table = unwind_symbol_hdr._symbol_table;
  RuntimeSymbolLookup &runtime_symbol_lookup =
      unwind_symbol_hdr._runtime_symbol_lookup;
  SymbolIdx_t symbol_idx = k_symbol_idx_null;
  if (jitdump_path.empty()) {
    symbol_idx =
        runtime_symbol_lookup.get_or_insert(dso._pid, pc, symbol_table);
  } else {
    symbol_idx = runtime_symbol_lookup.get_or_insert_jitdump(
        dso._pid, pc, symbol_table, jitdump_path);
  }
  if (symbol_idx == k_symbol_idx_null) {
    add_dso_frame(us, dso, pc, "pc");
    return {};
  }

  MapInfoIdx_t const map_idx = us->symbol_hdr._mapinfo_lookup.get_or_insert(
      us->pid, us->symbol_hdr._mapinfo_table, dso, {});

  return add_frame(symbol_idx, k_file_info_undef, map_idx, pc,
                   pc - dso.start() + dso.offset(), us);
}
} // namespace

DDRes unwind_init_dwfl(Process &process, bool avoid_new_attach,
                       UnwindState *us) {
  us->_dwfl_wrapper = process.get_or_insert_dwfl();
  if (!us->_dwfl_wrapper) {
    return ddres_warn(DD_WHAT_UW_ERROR);
  }
  if (avoid_new_attach && !us->_dwfl_wrapper->_attached) {
    return ddres_warn(DD_WHAT_UW_MAX_PIDS);
  }
  // Creates the dwfl unwinding backend
  return us->_dwfl_wrapper->attach(us->pid, us->ref_elf, us);
}

DDRes unwind_dwfl(Process &process, bool avoid_new_attach, UnwindState *us) {
  DDRes res = unwind_init_dwfl(process, avoid_new_attach, us);
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
