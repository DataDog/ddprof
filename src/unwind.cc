// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind.hpp"

#include "ddprof_stats.hpp"
#include "ddres.hpp"
#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"
#include "logger.hpp"
#include "signal_helper.hpp"
#include "symbol_hdr.hpp"
#include "unwind_dwfl.hpp"
#include "unwind_helpers.hpp"
#include "unwind_metrics.hpp"
#include "unwind_state.hpp"

#include <array>
#include <string_view.hpp>

using namespace std::string_view_literals;

namespace ddprof {
void unwind_init(void) { elf_version(EV_CURRENT); }

static void find_dso_add_error_frame(UnwindState *us) {
  DsoHdr::DsoFindRes find_res =
      us->dso_hdr.dso_find_closest(us->pid, us->current_ip);
  DDProfModRange mod_range =
      us->dso_hdr.find_mod_range(find_res.first, us->dso_hdr._map[us->pid]);
  add_error_frame(find_res.second ? &(find_res.first->second) : nullptr, us,
                  us->current_ip - mod_range._low_addr);
}

void unwind_init_sample(UnwindState *us, uint64_t *sample_regs,
                        pid_t sample_pid, uint64_t sample_size_stack,
                        char *sample_data_stack) {
  uw_output_clear(&us->output);
  memcpy(&us->initial_regs.regs[0], sample_regs,
         K_NB_REGS_UNWIND * sizeof(uint64_t));
  us->current_ip = us->initial_regs.regs[REGNAME(PC)];
  us->pid = sample_pid;
  us->stack_sz = sample_size_stack;
  us->stack = sample_data_stack;
}

static bool is_stack_complete(UnwindState *us) {
  static constexpr std::array s_expected_root_frames{"_start"sv, "__clone"sv};

  if (us->output.nb_locs == 0) {
    return false;
  }

  auto &symbol_table = us->symbol_hdr._symbol_table;
  auto &root_func =
      symbol_table[us->output.locs[us->output.nb_locs - 1]._symbol_idx]
          ._symname;

  return std::find(s_expected_root_frames.begin(), s_expected_root_frames.end(),
                   root_func) != s_expected_root_frames.end();
}

DDRes unwindstate__unwind(UnwindState *us) {
  DDRes res = ddres_init();
  if (us->pid != 0) { // we can not unwind pid 0
    res = unwind_dwfl(us);
  }
  if (IsDDResNotOK(res)) {
    find_dso_add_error_frame(us);
  }

  if (!is_stack_complete(us)) {
    ddprof_stats_add(STATS_UNWIND_INCOMPLETE_STACK, 1, nullptr);
  }

  // Add a frame that identifies executable to which these belong
  add_virtual_base_frame(us);
  if (us->_dwfl_wrapper->_inconsistent) {
    // error detected on this pid
    LG_WRN("(Inconsistent DWFL/DSOs)%d - Free associated objects", us->pid);
    unwind_pid_free(us, us->pid);
  }
  return res;
}

void unwind_pid_free(UnwindState *us, pid_t pid) {
  us->dso_hdr.pid_free(pid);
  us->dwfl_hdr.clear_pid(pid);
  us->symbol_hdr._base_frame_symbol_lookup.erase(pid);
}

void unwind_cycle(UnwindState *us) {
  us->symbol_hdr.display_stats();
  us->symbol_hdr.cycle();
  // clean up pids that we did not see recently
  us->dwfl_hdr.display_stats();
  us->dwfl_hdr.clear_unvisited();

  us->dso_hdr._stats.reset();
  unwind_metrics_reset();
}

} // namespace ddprof
