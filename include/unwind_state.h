#pragma once

#include <stdbool.h>
#include <sys/types.h>

#include "ddres_def.h"
#include "unwind_output.h"

typedef struct UnwindSymbolsHdr UnwindSymbolsHdr;
typedef struct DsoHdr DsoHdr;
typedef struct DwflHdr DwflHdr;
typedef struct Dwfl Dwfl;

#define K_NB_REGS_UNWIND 3

typedef struct UnwindRegisters {
  union {
    uint64_t regs[K_NB_REGS_UNWIND];
    struct {
      uint64_t ebp; // base address of the function's frame
      uint64_t esp; // top of the stack
      uint64_t eip; // Extended Instruction Pointer
    };
  };
} UnwindRegisters;

typedef struct UnwindState {
  DwflHdr *dwfl_hdr;
  Dwfl *dwfl;

  DsoHdr *dso_hdr;
  UnwindSymbolsHdr *symbols_hdr;

  pid_t pid;
  char *stack;
  size_t stack_sz;

  UnwindRegisters initial_regs;
  UnwindRegisters current_regs;

  bool attached;
  UnwindOutput output;
} UnwindState;

static inline bool unwind_registers_equal(const UnwindRegisters *lhs,
                                          const UnwindRegisters *rhs) {
  for (unsigned i = 0; i < K_NB_REGS_UNWIND; ++i) {
    if (lhs->regs[i] != rhs->regs[i]) {
      return false;
    }
  }
  return true;
}

static inline void unwind_registers_clear(UnwindRegisters *unwind_registers) {
  for (unsigned i = 0; i < K_NB_REGS_UNWIND; ++i) {
    unwind_registers->regs[i] = 0;
  }
}
