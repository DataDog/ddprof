// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind.hpp"

extern "C" {
#include "ddres.h"
#include "libebl.h"
#include "logger.h"
#include "signal_helper.h"
#include "unwind_metrics.h"
}

#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"
#include "symbol_hdr.hpp"
#include "unwind_dwfl.hpp"
#include "unwind_helpers.hpp"
#include "unwind_state.hpp"

#define UNUSED(x) (void)(x)

namespace ddprof {
void unwind_init(void) { elf_version(EV_CURRENT); }

static void find_dso_add_error_frame(UnwindState *us) {
  DsoHdr::DsoFindRes find_res =
      us->dso_hdr.dso_find_closest(us->pid, us->current_regs.eip);
  add_error_frame(find_res.second ? &(find_res.first->second) : nullptr, us,
                  us->current_regs.eip);
}

void unwind_init_sample(UnwindState *us, uint64_t *sample_regs,
                        pid_t sample_pid, uint64_t sample_size_stack,
                        char *sample_data_stack) {
  uw_output_clear(&us->output);
  memcpy(&us->initial_regs.regs[0], sample_regs,
         K_NB_REGS_UNWIND * sizeof(uint64_t));
  us->current_regs = us->initial_regs;
  us->pid = sample_pid;
  us->stack_sz = sample_size_stack;
  us->stack = sample_data_stack;
}

DDRes unwindstate__unwind(UnwindState *us) {
  DDRes res = ddres_init();
  if (us->pid != 0) { // we can not unwind pid 0
    res = unwind_dwfl(us);
  }
  if (IsDDResNotOK(res)) {
    find_dso_add_error_frame(us);
  }
  // Add a frame that identifies executable to which these belong
  add_virtual_base_frame(us);
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
  us->dwfl_hdr.clear_unvisited();

  us->dso_hdr._stats.reset();
  unwind_metrics_reset();
}

} // namespace ddprof
