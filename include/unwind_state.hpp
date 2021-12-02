// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

extern "C" {
#include "unwind_output.h"
#include <sys/types.h>
}

#include "ddres_def.h"
#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"
#include "symbol_hdr.hpp"

typedef struct Dwfl Dwfl;

#define K_NB_REGS_UNWIND 3

struct UnwindRegisters {
  UnwindRegisters() {
    for (int i = 0; i < K_NB_REGS_UNWIND; ++i) {
      regs[i] = 0;
    }
  }
  union {
    uint64_t regs[K_NB_REGS_UNWIND];
    struct {
      uint64_t ebp; // base address of the function's frame
      uint64_t esp; // top of the stack
      uint64_t eip; // Extended Instruction Pointer
    };
  };
};

typedef struct UnwindState {
  UnwindState() : dwfl(nullptr), pid(-1), stack(nullptr), stack_sz(0) {
    uw_output_clear(&output);
  }
  ddprof::DwflHdr dwfl_hdr;
  Dwfl *dwfl; // pointer to current dwfl element

  ddprof::DsoHdr dso_hdr;
  SymbolHdr symbol_hdr;

  pid_t pid;
  char *stack;
  size_t stack_sz;

  UnwindRegisters initial_regs;
  UnwindRegisters current_regs;

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
