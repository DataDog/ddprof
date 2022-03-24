// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "perf.h"
#include "savecontext.hpp"
#include "unwind.hpp"
#include "unwind_state.hpp"

#include <algorithm>
#include <unistd.h>

void funcA() __attribute__((noinline));
void funcB() __attribute__((noinline));

std::byte stack[PERF_SAMPLE_STACK_SIZE];

void funcB() {
  UnwindState state;
  uint64_t regs[K_NB_REGS_UNWIND];
  size_t stack_size = save_context(regs, stack);

  ddprof::unwind_init_sample(&state, regs, getpid(), stack_size,
                             reinterpret_cast<char *>(stack));
  ddprof::unwindstate__unwind(&state);

  auto &symbol_table = state.symbol_hdr._symbol_table;

  for (size_t iloc = 0; iloc < state.output.nb_locs; ++iloc) {
    auto &symbol = symbol_table[state.output.locs[iloc]._symbol_idx];
    printf("%zu: %s\n", iloc, symbol._demangle_name.c_str());
  }

  EXPECT_GT(state.output.nb_locs, 3);
  auto &symbol0 = symbol_table[state.output.locs[0]._symbol_idx];
  EXPECT_TRUE(symbol0._demangle_name.starts_with("save_context("));
  auto &symbol1 = symbol_table[state.output.locs[1]._symbol_idx];
  EXPECT_EQ(symbol1._demangle_name, "funcB()");
  auto &symbol2 = symbol_table[state.output.locs[2]._symbol_idx];
  EXPECT_EQ(symbol2._demangle_name, "funcA()");
}

void funcA() {
  funcB();
  // prevent tail call optimization
  getpid();
}

#ifdef __x86_64__
TEST(getcontext, getcontext) { funcA(); }
#endif