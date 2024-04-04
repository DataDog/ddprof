// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "create_elf.hpp"
#include "ddprof_defs.hpp"
#include "ddprof_process.hpp"
#include "ddres_def.hpp"
#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"
#include "dwfl_thread_callbacks.hpp"
#include "perf.hpp"
#include "perf_archmap.hpp"
#include "symbol_hdr.hpp"
#include "unwind_output.hpp"

#include <optional>
#include <sys/types.h>

using Dwfl = struct Dwfl;

namespace ddprof {

// This is not a strict mirror of the register values acquired by perf; rather
// it's an array whose individual positions each have semantic value in the
// context of DWARF; accordingly, the size is arch-dependent.
// It is possible to provide SIMD registers on x86, but we don't do that here.

// This is the max register index supported across all architectures
static constexpr size_t k_nb_registers_to_unwind = k_perf_register_count;

// The layout below follows kernel arch/<ARCH>/include/uapi/asm/perf_regs.h
struct UnwindRegisters {
  uint64_t regs[k_nb_registers_to_unwind] = {};
};

/// UnwindState
/// Single structure with everything necessary in unwinding. The structure is
/// given through callbacks
struct UnwindState {
  explicit UnwindState(UniqueElf ref_elf, int dd_profiling_fd = -1)
      : dso_hdr("", dd_profiling_fd), ref_elf(std::move(ref_elf)) {
    output.clear();
    output.locs.reserve(kMaxStackDepth);
  }
  DwflWrapper *_dwfl_wrapper{nullptr}; // pointer to current dwfl element
  DsoHdr dso_hdr;
  SymbolHdr symbol_hdr;
  ProcessHdr process_hdr;

  pid_t pid{-1};
  const char *stack{nullptr};
  size_t stack_sz{0};

  UnwindRegisters initial_regs;
  ProcessAddress_t current_ip{0};

  UnwindOutput output;
  UniqueElf ref_elf; // reference elf object used to initialize dwfl
};

std::optional<UnwindState> create_unwind_state(int dd_profiling_fd = -1);

static inline bool unwind_registers_equal(const UnwindRegisters *lhs,
                                          const UnwindRegisters *rhs) {
  for (unsigned i = 0; i < k_nb_registers_to_unwind; ++i) {
    if (lhs->regs[i] != rhs->regs[i]) {
      return false;
    }
  }
  return true;
}

static inline void unwind_registers_clear(UnwindRegisters *unwind_registers) {
  for (unsigned long &reg : unwind_registers->regs) {
    reg = 0;
  }
}
} // namespace ddprof
