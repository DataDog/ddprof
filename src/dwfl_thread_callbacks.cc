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

// DWARF and the Linux kernel don't have a uniform view of the processor, so we
// can't just follow the register mask and shove it into the output registers.
// Instead, we crib off of libdwfl's ARM/x86 unwind code in elfutil's
// libdwfl/unwind-libdw.c
bool set_initial_registers(Dwfl_Thread *thread, void *arg) {
  Dwarf_Word regs[33] = {}; // max register count across all arcs
  uint64_t n = 0;
  struct UnwindState *us = reinterpret_cast<UnwindState *>(arg);

#ifdef __x86_64__
  // Substantial difference here in 32- and 64-bit x86; only support 64-bit now
  regs[n++] = us->initial_regs.regs[REGNAME(EAX)];
  regs[n++] = us->initial_regs.regs[REGNAME(EDX)];
  regs[n++] = us->initial_regs.regs[REGNAME(ECX)];
  regs[n++] = us->initial_regs.regs[REGNAME(EBX)];
  regs[n++] = us->initial_regs.regs[REGNAME(ESI)];
  regs[n++] = us->initial_regs.regs[REGNAME(EDI)];
  regs[n++] = us->initial_regs.regs[REGNAME(EBP)];
  regs[n++] = us->initial_regs.regs[REGNAME(SP)];
  regs[n++] = us->initial_regs.regs[REGNAME(R8)];
  regs[n++] = us->initial_regs.regs[REGNAME(R9)];
  regs[n++] = us->initial_regs.regs[REGNAME(R10)];
  regs[n++] = us->initial_regs.regs[REGNAME(R11)];
  regs[n++] = us->initial_regs.regs[REGNAME(R12)];
  regs[n++] = us->initial_regs.regs[REGNAME(R13)];
  regs[n++] = us->initial_regs.regs[REGNAME(R14)];
  regs[n++] = us->initial_regs.regs[REGNAME(R15)];
  regs[n++] = us->initial_regs.regs[REGNAME(PC)];
#elif __aarch64__
  regs[n++] = us->initial_regs.regs[REGNAME(X0)];
  regs[n++] = us->initial_regs.regs[REGNAME(X1)];
  regs[n++] = us->initial_regs.regs[REGNAME(X2)];
  regs[n++] = us->initial_regs.regs[REGNAME(X3)];
  regs[n++] = us->initial_regs.regs[REGNAME(X4)];
  regs[n++] = us->initial_regs.regs[REGNAME(X5)];
  regs[n++] = us->initial_regs.regs[REGNAME(X6)];
  regs[n++] = us->initial_regs.regs[REGNAME(X7)];
  regs[n++] = us->initial_regs.regs[REGNAME(X8)];
  regs[n++] = us->initial_regs.regs[REGNAME(X9)];
  regs[n++] = us->initial_regs.regs[REGNAME(X10)];
  regs[n++] = us->initial_regs.regs[REGNAME(X11)];
  regs[n++] = us->initial_regs.regs[REGNAME(X12)];
  regs[n++] = us->initial_regs.regs[REGNAME(X13)];
  regs[n++] = us->initial_regs.regs[REGNAME(X14)];
  regs[n++] = us->initial_regs.regs[REGNAME(X15)];
  regs[n++] = us->initial_regs.regs[REGNAME(X16)];
  regs[n++] = us->initial_regs.regs[REGNAME(X17)];
  regs[n++] = us->initial_regs.regs[REGNAME(X18)];
  regs[n++] = us->initial_regs.regs[REGNAME(X19)];
  regs[n++] = us->initial_regs.regs[REGNAME(X20)];
  regs[n++] = us->initial_regs.regs[REGNAME(X21)];
  regs[n++] = us->initial_regs.regs[REGNAME(X22)];
  regs[n++] = us->initial_regs.regs[REGNAME(X23)];
  regs[n++] = us->initial_regs.regs[REGNAME(X24)];
  regs[n++] = us->initial_regs.regs[REGNAME(X25)];
  regs[n++] = us->initial_regs.regs[REGNAME(X26)];
  regs[n++] = us->initial_regs.regs[REGNAME(X27)];
  regs[n++] = us->initial_regs.regs[REGNAME(X28)];
  regs[n++] = us->initial_regs.regs[REGNAME(FP)];
  regs[n++] = us->initial_regs.regs[REGNAME(LR)];
  regs[n++] = us->initial_regs.regs[REGNAME(SP)];

  // Although the perf registers designate the register after SP as the PC, this
  // convention is not a documented convention of the DWARF registers.  We set
  // the PC manually.
#else
#  error Architecture not supported
#endif

  if (!dwfl_thread_state_registers(thread, 0, n, regs))
    return false;

  dwfl_thread_state_register_pc(thread, us->initial_regs.regs[REGNAME(PC)]);
  return true;
}

/// memory_read as per prototype define in libdwfl
bool memory_read_dwfl(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result,
                      void *arg) {
  (void)dwfl;
  return ddprof::memory_read(addr, result, arg);
}
