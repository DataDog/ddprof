// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind_dwfl.hpp"

#include "austin_symbol_lookup.hpp"
#include "ddprof_stats.hpp"
#include "ddres.hpp"
#include "dwfl_internals.hpp"
#include "dwfl_thread_callbacks.hpp"

#include "logger.hpp"
#include "runtime_symbol_lookup.hpp"
#include "symbol_hdr.hpp"
#include "unwind_helpers.hpp"
#include "unwind_state.hpp"

#include "libaustin.h"
extern "C" {
#include "libebl.h"
}

// clang-format off

/// Hacky way of accessing registers
//  --> imported definition from the libdwflP.h

struct Dwfl_Process
{
  struct Dwfl *dwfl;
  pid_t pid;
  const Dwfl_Thread_Callbacks *callbacks;
  void *callbacks_arg;
  struct ebl *ebl;
  bool ebl_close:1;
};

/* See its typedef in libdwfl.h.  */

struct Dwfl_Thread
{
  Dwfl_Process *process;
  pid_t tid;
  /* Bottom (innermost) frame while we're initializing, NULL afterwards.  */
  Dwfl_Frame *unwound;
  void *callbacks_arg;
};


struct Dwfl_Frame
{
  Dwfl_Thread *thread;
  /* Previous (outer) frame.  */
  Dwfl_Frame *unwound;
  bool signal_frame : 1;
  bool initial_frame : 1;
  enum
  {
    /* This structure is still being initialized or there was an error
       initializing it.  */
    DWFL_FRAME_STATE_ERROR,
    /* PC field is valid.  */
    DWFL_FRAME_STATE_PC_SET,
    /* PC field is undefined, this means the next (inner) frame was the
       outermost frame.  */
    DWFL_FRAME_STATE_PC_UNDEFINED
  } pc_state;
  /* Either initialized from appropriate REGS element or on some archs
     initialized separately as the return address has no DWARF register.  */
  Dwarf_Addr pc;
  /* (1 << X) bitmask where 0 <= X < ebl_frame_nregs.  */
  uint64_t regs_set[3];
  /* REGS array size is ebl_frame_nregs.
     REGS_SET tells which of the REGS are valid.  */
  Dwarf_Addr regs[];
};

/* Fetch value from Dwfl_Frame->regs indexed by DWARF REGNO.
   No error code is set if the function returns FALSE.  */
static bool
__libdwfl_frame_reg_get (Dwfl_Frame *state, unsigned regno, Dwarf_Addr *val)
{
  Ebl *ebl = state->thread->process->ebl;
  if (! ebl_dwarf_to_regno (ebl, &regno))
    return false;
  if (regno >= ebl_frame_nregs (ebl))
    return false;
  if ((state->regs_set[regno / sizeof (*state->regs_set) / 8]
       & ((uint64_t) 1U << (regno % (sizeof (*state->regs_set) * 8)))) == 0)
    return false;
  if (val)
    *val = state->regs[regno];
  return true;
}

// clang-format on

int frame_cb(Dwfl_Frame *, void *);

namespace ddprof {

DDRes unwind_init_dwfl(UnwindState *us) {
  // Create or get the dwfl object associated to cache
  us->_dwfl_wrapper = &(us->dwfl_hdr.get_or_insert(us->pid));
  // clear at every iteration
  us->symbol_hdr._austin_symbol_lookup.clear();

  if (!us->_dwfl_wrapper->_attached) {
    // we need to add at least one module to figure out the architecture (to
    // create the unwinding backend)

    DsoHdr::DsoMap &map = us->dso_hdr._pid_map[us->pid]._map;
    if (map.empty()) {
      int nb_elts;
      us->dso_hdr.pid_backpopulate(us->pid, nb_elts);
    }

    bool success = false;
    // Find an elf file we can load for this PID
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
      const Dso &dso = it->second;
      if (dso._executable) {
        FileInfoId_t file_info_id = us->dso_hdr.get_or_insert_file_info(dso);
        if (file_info_id <= k_file_info_error) {
          LG_DBG("Unable to find file for DSO %s", dso.to_string().c_str());
          continue;
        }
        const FileInfoValue &file_info_value =
            us->dso_hdr.get_file_info_value(file_info_id);

        DDProfMod *ddprof_mod = us->_dwfl_wrapper->register_mod(
            us->current_ip, dso, file_info_value);
        if (ddprof_mod) {
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
        us->dso_hdr.dso_find_closest(us->pid, us->current_ip);
    SymbolIdx_t symIdx =
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

void print_all_registers(Dwfl_Frame *dwfl_frame) {
  for (int i = 0; i != PERF_REGS_COUNT; ++i) {
    uint64_t val;
    if (__libdwfl_frame_reg_get(dwfl_frame, i, &val)) {
      LG_DBG("Register %i = %lx", i, val);
    }
  }
}

static DDRes add_dwfl_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc,
                            const DDProfMod &ddprof_mod,
                            FileInfoId_t file_info_id, Dwfl_Frame *dwfl_frame);

// check for runtime symbols provided in /tmp files
static DDRes add_runtime_symbol_frame(UnwindState *us, const Dso &dso,
                                      ElfAddress_t pc,
                                      std::string_view jitdump_path);

static DDRes add_python_frame(UnwindState *us, SymbolIdx_t symbol_idx,
                              ElfAddress_t pc, Dwfl_Frame *dwfl_frame);

// returns an OK status if we should continue unwinding
static DDRes add_symbol(Dwfl_Frame *dwfl_frame, UnwindState *us) {
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
    return ddres_init(); // invalid pc : do not add frame
  }
  us->current_ip = pc;
  DsoHdr &dsoHdr = us->dso_hdr;
  DsoHdr::PidMapping &pid_mapping = dsoHdr._pid_map[us->pid];
  if (!pc) {
    // Unwinding can end on a null address
    // Example: alpine 3.17
    return ddres_init();
  }
  DsoHdr::DsoFindRes find_res =
      dsoHdr.dso_find_or_backpopulate(pid_mapping, us->pid, pc);
  if (!find_res.second) {
    // no matching file was found
    LG_DBG("[UW] (PID%d) DSO not found at 0x%lx (depth#%lu)", us->pid, pc,
           us->output.locs.size());
    add_error_frame(nullptr, us, pc, SymbolErrors::unknown_dso);
    return ddres_init();
  }
  const Dso &dso = find_res.first->second;
  std::string_view jitdump_path = {};
  if (dso::has_runtime_symbols(dso._type)) {
    if (pid_mapping._jitdump_addr) {
      DsoHdr::DsoFindRes find_mapping =
          DsoHdr::dso_find_closest(pid_mapping._map, pid_mapping._jitdump_addr);
      if (find_mapping.second) { // jitdump exists
        jitdump_path = find_mapping.first->second._filename;
      }
    }
    return add_runtime_symbol_frame(us, dso, pc, jitdump_path);
  }
  // if not encountered previously, update file location / key
  FileInfoId_t file_info_id = us->dso_hdr.get_or_insert_file_info(dso);
  if (file_info_id <= k_file_info_error) {
    // unable to acces file: add available info from dso
    add_dso_frame(us, dso, pc, "pc");
    // We could stop here or attempt to continue in the dwarf unwinding
    // sometimes frame pointer lets us go further -> So we continue
    return ddres_init();
  }
  const FileInfoValue &file_info_value =
      us->dso_hdr.get_file_info_value(file_info_id);
  DDProfMod *ddprof_mod = us->_dwfl_wrapper->unsafe_get(file_info_id);
  if (!ddprof_mod) {
    // ensure unwinding backend has access to this module (and check
    // consistency)
    ddprof_mod = us->_dwfl_wrapper->register_mod(pc, dso, file_info_value);
    // Updates in DSO layout can create inconsistencies
    if (!ddprof_mod) {
      return ddres_warn(DD_WHAT_UW_ERROR);
    }
  }

  // To check that we are in an activation frame, we unwind the current frame
  // This means we need access to the module information.
  // Now that we have loaded the module, we can check if we are an activation
  // frame
  bool isactivation = false;

  if (!dwfl_frame_pc(dwfl_frame, &pc, &isactivation)) {
    LG_DBG("Failure to compute frame PC: %s (depth#%lu)", dwfl_errmsg(-1),
           us->output.locs.size());
    add_error_frame(nullptr, us, pc, SymbolErrors::dwfl_frame);
    return ddres_init(); // invalid pc : do not add frame
  }
  if (!isactivation)
    --pc;
  us->current_ip = pc;

  // Now we register
  if (IsDDResNotOK(
          add_dwfl_frame(us, dso, pc, *ddprof_mod, file_info_id, dwfl_frame))) {
    return ddres_warn(DD_WHAT_UW_ERROR);
  }
  return ddres_init();
}

bool is_infinite_loop(UnwindState *us) {
  UnwindOutput &output = us->output;
  uint64_t nb_locs = output.locs.size();
  unsigned nb_frames_to_check = 3;
  if (nb_locs <= nb_frames_to_check) {
    return false;
  }
  for (unsigned i = 1; i < nb_frames_to_check; ++i) {
    FunLoc &n_minus_one_loc = output.locs[nb_locs - i];
    FunLoc &n_minus_two_loc = output.locs[nb_locs - i - 1];
    if (n_minus_one_loc.ip != n_minus_two_loc.ip) {
      return false;
    }
  }
  return true;
}

// frame_cb callback at every frame for the dwarf unwinding
static int frame_cb(Dwfl_Frame *dwfl_frame, void *arg) {
  UnwindState *us = (UnwindState *)arg;
#ifdef DEBUG
  LG_NFO("Beging depth %lu", us->output.locs.size());
#endif
  int dwfl_error_value = dwfl_errno();
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
  res = us->output.locs.size() > 0 ? ddres_init()
                                   : ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
  return res;
}

static DDRes add_dwfl_frame(UnwindState *us, const Dso &dso, ElfAddress_t pc,
                            const DDProfMod &ddprof_mod,
                            FileInfoId_t file_info_id, Dwfl_Frame *dwfl_frame) {

  SymbolHdr &unwind_symbol_hdr = us->symbol_hdr;

  // get or create the dwfl symbol
  SymbolIdx_t symbol_idx = unwind_symbol_hdr._dwfl_symbol_lookup.get_or_insert(
      ddprof_mod, unwind_symbol_hdr._symbol_table,
      unwind_symbol_hdr._dso_symbol_lookup, file_info_id, pc, dso);

  if (IsDDResOK(add_python_frame(us, symbol_idx, pc, dwfl_frame))) {
    return ddres_init();
  }

  MapInfoIdx_t map_idx = us->symbol_hdr._mapinfo_lookup.get_or_insert(
      us->pid, us->symbol_hdr._mapinfo_table, dso, ddprof_mod._build_id);
  return add_frame(symbol_idx, map_idx, pc, us);
}

// check for runtime symbols provided in /tmp files
static DDRes add_runtime_symbol_frame(UnwindState *us, const Dso &dso,
                                      ElfAddress_t pc,
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
    return ddres_init();
  }

  MapInfoIdx_t map_idx = us->symbol_hdr._mapinfo_lookup.get_or_insert(
      us->pid, us->symbol_hdr._mapinfo_table, dso, {});

  return add_frame(symbol_idx, map_idx, pc, us);
}

// check for Python frame evaluation symbols
static DDRes add_python_frame(UnwindState *us, SymbolIdx_t symbol_idx,
                              ElfAddress_t pc, Dwfl_Frame *dwfl_frame) {
  SymbolHdr &unwind_symbol_hdr = us->symbol_hdr;
  SymbolTable &symbol_table = unwind_symbol_hdr._symbol_table;

  if (symbol_table.at(symbol_idx)._is_python_frame) {
    // only attempt to attach on python frames
    Process &p = us->process_hdr.get_process(us->pid);
    austin_handle_t handle = p.get_austin_handle(us->tid);
    if (!handle) {
      return ddres_error(1);
    }
    AustinSymbolLookup &austin_symbol_lookup =
        unwind_symbol_hdr._austin_symbol_lookup;
    // The register we are interested in is RSI, but it doesn't seem to be
    // available. So we loop over the available registers and stop if we find
    // a register value that resolves correctly to a Python frame.
    std::vector<int> &reg_indices = p._python_register_indices;
    SymbolIdx_t python_sym_idx = -1;
    static unsigned python_lookup_cpt = 0;
    uint64_t val = 0;

    // Hacky way of caching the regs that are actually useful
    // They are stable
    if (reg_indices.empty() || !(++python_lookup_cpt < 10) ||
        !(python_lookup_cpt % 100)) {
      for (int i = 0; i < PERF_REGS_COUNT; i++) {
        if (__libdwfl_frame_reg_get(dwfl_frame, i, &val) && val) {
          austin_frame_t *frame = austin_read_frame(handle, (void *)val);
          if (frame) {
            reg_indices.push_back(i);
            python_sym_idx =
                austin_symbol_lookup.get_or_insert(frame, symbol_table);
            break;
          }
        }
      }
    } else {
      for (int i : reg_indices) {
        if (__libdwfl_frame_reg_get(dwfl_frame, i, &val) && val) {
          austin_frame_t *frame = austin_read_frame(handle, (void *)val);
          if (frame) {
            python_sym_idx =
                austin_symbol_lookup.get_or_insert(frame, symbol_table);
            break;
          }
        }
      }
    }
    if (python_sym_idx != -1) {
      return add_frame(python_sym_idx, -1, pc, us);
    }
  }

  return ddres_error(1);
}

} // namespace ddprof
