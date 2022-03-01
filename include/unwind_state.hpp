// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

extern "C" {
#include "unwind_output.h"
#include <sys/types.h>
}

#include "ddprof_defs.h"
#include "ddres_def.h"
#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"
#include "dwfl_thread_callbacks.hpp"
#include "perf.h"
#include "symbol_hdr.hpp"

typedef struct Dwfl Dwfl;

// This is not a strict mirror of the register values acquired by perf; rather
// it's an array whose individual positions each have semantic value in the
// context of DWARF; accordingly, the size is arch-dependent.
// It is possible to provide SIMD registers on x86, but we don't do that here.

// This is the max register index supported across all architectures
#  define K_NB_REGS_UNWIND 33

// The layout below follows kernel arch/<ARCH>/include/uapi/asm/perf_regs.h
struct UnwindRegisters {
  UnwindRegisters() {
    for (int i = 0; i < K_NB_REGS_UNWIND; ++i) {
      regs[i] = 0;
    }
  }
  union {
    uint64_t regs[K_NB_REGS_UNWIND];
    struct {
#ifdef __x86_64__
      uint64_t eax;
      uint64_t edx;
      uint64_t ecx;
      uint64_t ebx;
      uint64_t esi;
      uint64_t edi;
      uint64_t fp; // ebp
      uint64_t sp; // esp
      uint64_t r8;
      uint64_t r9;
      uint64_t r10;
      uint64_t r11;
      uint64_t r12;
      uint64_t r13;
      uint64_t r14;
      uint64_t r15;
      uint64_t pc; // eip
#elif __aarch64__
      uint64_t x0;
      uint64_t x1;
      uint64_t x2;
      uint64_t x3;
      uint64_t x4;
      uint64_t x5;
      uint64_t x6;
      uint64_t x7;
      uint64_t x8;
      uint64_t x9;
      uint64_t x10;
      uint64_t x11;
      uint64_t x12;
      uint64_t x13;
      uint64_t x14;
      uint64_t x15;
      uint64_t x16;
      uint64_t x17;
      uint64_t x18;
      uint64_t x19;
      uint64_t x20;
      uint64_t x21;
      uint64_t x22;
      uint64_t x23;
      uint64_t x24;
      uint64_t x25;
      uint64_t x26;
      uint64_t x27;
      uint64_t x28;
      uint64_t fp; // 29, For uniformity with libdwfl
      uint64_t lr; // 30
      uint64_t sp; // 31
      uint64_t pc; // For uniformity with libdwfl/ARM spec
#else
#  error Architecture not supported
#endif
    };
  };
};

/// UnwindState
/// Single structure with everything necessary in unwinding. The structure is
/// given through callbacks
typedef struct UnwindState {
  UnwindState()
      : _dwfl_wrapper(nullptr), pid(-1), stack(nullptr), stack_sz(0),
        current_ip(0) {
    uw_output_clear(&output);
  }

  ddprof::DwflHdr dwfl_hdr;
  ddprof::DwflWrapper *_dwfl_wrapper; // pointer to current dwfl element

  ddprof::DsoHdr dso_hdr;
  SymbolHdr symbol_hdr;

  pid_t pid;
  char *stack;
  size_t stack_sz;

  UnwindRegisters initial_regs;
  ProcessAddress_t current_ip;

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
