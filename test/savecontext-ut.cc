// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "ddprof_base.hpp"
#include "defer.hpp"
#include "perf.hpp"
#include "savecontext.hpp"
#include "unwind.hpp"
#include "unwind_state.hpp"

#include <algorithm>
#include <unistd.h>

DDPROF_NOINLINE void funcA();
DDPROF_NOINLINE void funcB();

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
  DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
}

TEST(getcontext, getcontext) { funcA(); }

DDPROF_NOINLINE void funcD() { asm("int3"); }

DDPROF_NOINLINE void funcC() {
  funcD();
  DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
}

static uint64_t regs[K_NB_REGS_UNWIND];
static size_t stack_size;

void handler(int sig) { stack_size = save_context(regs, stack); }

TEST(getcontext, unwind_from_sighandler) {
  signal(SIGTRAP, &handler);
  defer { signal(SIGTRAP, SIG_DFL); };
  funcC();
  UnwindState state;
  ddprof::unwind_init_sample(&state, regs, getpid(), stack_size,
                             reinterpret_cast<char *>(stack));
  ddprof::unwindstate__unwind(&state);

  auto &symbol_table = state.symbol_hdr._symbol_table;

  for (size_t iloc = 0; iloc < state.output.nb_locs; ++iloc) {
    auto &symbol = symbol_table[state.output.locs[iloc]._symbol_idx];
    printf("%zu: %s\n", iloc, symbol._demangle_name.c_str());
  }

  EXPECT_GT(state.output.nb_locs, 5);
  auto &symbol0 = symbol_table[state.output.locs[0]._symbol_idx];
  EXPECT_TRUE(symbol0._demangle_name.starts_with("save_context("));
  auto &symbol1 = symbol_table[state.output.locs[1]._symbol_idx];
  EXPECT_EQ(symbol1._demangle_name, "handler(int)");
  auto &symbol3 = symbol_table[state.output.locs[3]._symbol_idx];
  EXPECT_EQ(symbol3._demangle_name, "funcD()");
  auto &symbol4 = symbol_table[state.output.locs[4]._symbol_idx];
  EXPECT_EQ(symbol4._demangle_name, "funcC()");
}
