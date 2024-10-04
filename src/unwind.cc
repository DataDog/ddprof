// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind.hpp"

#include "ddprof_stats.hpp"
#include "ddres.hpp"
#include "dso_hdr.hpp"
#include "dwfl_wrapper.hpp"
#include "logger.hpp"
#include "signal_helper.hpp"
#include "symbol_hdr.hpp"
#include "unwind_dwfl.hpp"
#include "unwind_helper.hpp"
#include "unwind_metrics.hpp"
#include "unwind_state.hpp"

#include <algorithm>
#include <array>

namespace ddprof {

namespace {
void find_dso_add_error_frame(DDRes ddres, UnwindState *us) {
  if (ddres._what == DD_WHAT_UW_MAX_PIDS) {
    add_common_frame(us, SymbolErrors::max_pids);
  } else {
    DsoHdr::DsoFindRes const find_res =
        us->dso_hdr.dso_find_closest(us->pid, us->current_ip);
    add_error_frame(find_res.second ? &(find_res.first->second) : nullptr, us,
                    us->current_ip);
  }
}

void add_container_id(Process &process, UnwindState *us) {
  const auto &container_id = process.get_container_id();
  if (container_id) {
    us->output.container_id = *container_id;
  }
}

void add_exe_name(Process &process, UnwindState *us) {
  us->output.exe_name =
      us->symbol_hdr._base_frame_symbol_lookup.get_exe_name(us->pid);
}

void add_thread_name(Process &process, UnwindState *us) {
  us->output.thread_name = process.get_or_insert_thread_name(us->output.tid);
}
} // namespace

void unwind_init() { elf_version(EV_CURRENT); }

void unwind_init_sample(UnwindState *us, const uint64_t *sample_regs,
                        pid_t sample_pid, uint64_t sample_size_stack,
                        const char *sample_data_stack) {
  us->output.clear();
  memcpy(&us->initial_regs.regs[0], sample_regs,
         k_nb_registers_to_unwind * sizeof(uint64_t));
  us->current_ip = us->initial_regs.regs[REGNAME(PC)];
  us->pid = sample_pid;
  us->stack_sz = sample_size_stack;
  us->stack = sample_data_stack;
}

DDRes unwindstate_unwind(UnwindState *us) {
  DDRes res = ddres_init();
  Process &process = us->process_hdr.get(us->pid);
  bool avoid_new_attach = false;
  if (us->maximum_pids != k_unlimited_max_profiled_pids &&
      us->process_hdr.process_count() >
          static_cast<unsigned>(us->maximum_pids)) {
    // we limit number of pids heavily as we can not guarantee unwinding
    // does not open new files
    // There is currently no priority in the processes that we should unwind
    // So there could be a situation where we are stuck with PIDs that are not
    // interesting.
    avoid_new_attach = true;
  }
  if (us->pid != 0) { // we can not unwind pid 0
    res = unwind_dwfl(process, avoid_new_attach, us);
  }
  if (IsDDResNotOK(res)) {
    if (res._what == DD_WHAT_UW_MAX_PIDS) {
      add_common_frame(us, SymbolErrors::max_pids);
    } else {
      ddprof_stats_add(STATS_UNWIND_ERRORS, 1, nullptr);
      find_dso_add_error_frame(res, us);
    }
  }
  ddprof_stats_add(STATS_UNWIND_AVG_STACK_DEPTH, us->output.locs.size(),
                   nullptr);

  // Add a frame that identifies executable to which these belong
  add_virtual_base_frame(us);
  add_container_id(process, us);
  add_exe_name(process, us);
  add_thread_name(process, us);
  return res;
}

void unwind_pid_free(UnwindState *us, pid_t pid) {
  us->dso_hdr.pid_free(pid);
  us->symbol_hdr.clear(pid);
  us->process_hdr.clear(pid);
}

void unwind_cycle(UnwindState *us) {
  us->symbol_hdr.display_stats();
  us->symbol_hdr.cycle();
  us->process_hdr.display_stats();
  us->dso_hdr.stats().reset();
  unwind_metrics_reset();
}

} // namespace ddprof
