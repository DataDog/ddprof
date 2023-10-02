// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_thread_callbacks.hpp"
#include "perf_archmap.hpp"

#include "unwind_helpers.hpp"
#include "unwind_state.hpp"

namespace ddprof {

pid_t next_thread(Dwfl *dwfl, void *arg, void **thread_argp) {
  (void)dwfl;
  if (*thread_argp != nullptr) {
    return 0;
  }
  auto *us = reinterpret_cast<UnwindState *>(arg);
  *thread_argp = arg;
  return us->pid;
}

// DWARF and the Linux kernel don't have a uniform view of the processor, so we
// can't just follow the register mask and shove it into the output registers.
// Instead, we crib off of libdwfl's ARM/x86 unwind code in elfutil's
// libdwfl/unwind-libdw.c
bool set_initial_registers(Dwfl_Thread *thread, void *arg) {
  Dwarf_Word regs[k_perf_register_count] =
      {}; // max register count across all arcs
  auto *us = reinterpret_cast<UnwindState *>(arg);
  // clang-format off
  unsigned int regs_num;
  for (regs_num = 0; - 1U != dwarf_to_perf_regno(regs_num); ++regs_num) {
    unsigned int const regs_idx = dwarf_to_perf_regno(regs_num);
    regs[regs_num] = us->initial_regs.regs[regs_idx];
  }
  // clang-format on
  // Although the perf registers designate the register after SP as the PC, this
  // convention is not a documented convention of the DWARF registers.  We set
  // the PC manually.
  if (!dwfl_thread_state_registers(thread, 0, regs_num, regs)) {
    return false;
  }

  dwfl_thread_state_register_pc(thread, us->initial_regs.regs[REGNAME(PC)]);
  return true;
}

/// memory_read as per prototype define in libdwfl
bool memory_read_dwfl(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result,
                      int regno, void *arg) {
  (void)dwfl;
  return memory_read(addr, result, regno, arg);
}

} // namespace ddprof
