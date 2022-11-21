// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "async-profiler/codeCache.h"
#include "ddprof_defs.hpp"
#include "ddres_def.hpp"
#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"
#include "dwfl_thread_callbacks.hpp"
#include "perf.hpp"
#include "perf_archmap.hpp"
#include "symbol_hdr.hpp"
#include "unwind_output.hpp"

#include <sys/types.h>

typedef struct Dwfl Dwfl;

// This is not a strict mirror of the register values acquired by perf; rather
// it's an array whose individual positions each have semantic value in the
// context of DWARF; accordingly, the size is arch-dependent.
// It is possible to provide SIMD registers on x86, but we don't do that here.

// This is the max register index supported across all architectures
#define K_NB_REGS_UNWIND PERF_REGS_COUNT

// The layout below follows kernel arch/<ARCH>/include/uapi/asm/perf_regs.h
struct UnwindRegisters {
  uint64_t regs[K_NB_REGS_UNWIND] = {};
};

/// UnwindState
/// Single structure with everything necessary in unwinding. The structure is
/// given through callbacks
struct UnwindState {
  explicit UnwindState(int dd_profiling_fd = -1)
      : pid(-1), stack(nullptr), stack_sz(0), current_ip(0) {}

  pid_t pid;
  char *stack;
  size_t stack_sz;

  UnwindRegisters initial_regs;
  ProcessAddress_t current_ip;

  std::unordered_map<pid_t, CodeCacheArray> code_cache;
  UnwindOutput_V2 output;
};

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
