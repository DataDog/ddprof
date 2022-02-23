#include "dwfl_thread_callbacks.hpp"

#include "unwind_helpers.hpp"
#include "unwind_state.hpp"

pid_t next_thread(Dwfl *dwfl, void *arg, void **thread_argp) {
  (void)dwfl;
  if (*thread_argp != NULL) {
    return 0;
  }
  struct UnwindState *us = reinterpret_cast<UnwindState *>(arg);
  *thread_argp = arg;
  return us->pid;
}

bool set_initial_registers(Dwfl_Thread *thread, void *arg) {
  struct UnwindState *us = reinterpret_cast<UnwindState *>(arg);
  Dwarf_Word regs[K_NB_REGS_UNWIND] = {};
  for (int i = 0; i < K_NB_REGS_UNWIND; ++i) {
    regs[i] = us->initial_regs.regs[i];
  }

  return dwfl_thread_state_registers(thread, 0, K_NB_REGS_UNWIND, regs);
}

/// memory_read as per prototype define in libdwfl
bool memory_read_dwfl(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result,
                      void *arg) {
  (void)dwfl;
  return ddprof::memory_read(addr, result, arg);
}
