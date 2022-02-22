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

#ifdef __x86_64__
  // Only 3 registers are used in the unwinding
  Dwarf_Word regs[17] = {0};
  regs[6] = us->initial_regs.ebp;
  regs[7] = us->initial_regs.esp;
  regs[16] = us->initial_regs.eip;
#elif __aarch64__
  Dwarf_Word regs[33] = {0};
  regs[29] = us->initial_regs.ebp;
  regs[30] = us->initial_regs.lr;
  regs[31] = us->initial_regs.esp;
  regs[32] = us->initial_regs.eip;
#endif

  return dwfl_thread_state_registers(thread, 0, sizeof(regs)/sizeof(*regs), regs);
}

/// memory_read as per prototype define in libdwfl
bool memory_read_dwfl(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result,
                      void *arg) {
  (void)dwfl;
  return ddprof::memory_read(addr, result, arg);
}
