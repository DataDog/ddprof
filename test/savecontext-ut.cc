// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "ddprof_base.hpp"
#include "defer.hpp"
#include "loghandle.hpp"
#include "perf.hpp"
#include "savecontext.hpp"
#include "unwind.hpp"
#include "unwind_state.hpp"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace ddprof {

DDPROF_NOINLINE void funcA();
DDPROF_NOINLINE void funcB();

std::byte stack[k_default_perf_stack_sample_size];

void funcB() {
  UnwindState state = create_unwind_state().value();
  uint64_t regs[k_nb_registers_to_unwind];
  size_t stack_size = save_context(retrieve_stack_bounds(), regs, stack);

  unwind_init_sample(&state, regs, getpid(), stack_size,
                     reinterpret_cast<char *>(stack));
  unwindstate_unwind(&state);

  auto &symbol_table = state.symbol_hdr._symbol_table;

  for (size_t iloc = 0; iloc < state.output.locs.size(); ++iloc) {
    auto &symbol = symbol_table[state.output.locs[iloc]._symbol_idx];
    printf("%zu: %s\n", iloc, symbol._demangle_name.c_str());
  }

  EXPECT_GT(state.output.locs.size(), 3);
  auto &symbol0 = symbol_table[state.output.locs[0]._symbol_idx];
  EXPECT_TRUE(symbol0._demangle_name.starts_with("ddprof::save_context("));
  auto &symbol1 = symbol_table[state.output.locs[1]._symbol_idx];
  EXPECT_EQ(symbol1._demangle_name, "ddprof::funcB()");
  auto &symbol2 = symbol_table[state.output.locs[2]._symbol_idx];
  EXPECT_EQ(symbol2._demangle_name, "ddprof::funcA()");
}

void funcA() {
  funcB();
  DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
}

TEST(getcontext, getcontext) { funcA(); }

#if defined(__x86_64__) && !defined(MUSL_LIBC)
// The matrix of where it works well is slightly more complex
// There are also differences depending on vdso (as this can be a kernel
// mechanism). We should revisit if we needed.

static std::atomic<bool> stop;
static std::mutex mutex;
static std::condition_variable cv;
static uint64_t regs[k_nb_registers_to_unwind];
static size_t stack_size;
static std::span<const std::byte> thread_stack_bounds;

DDPROF_NO_SANITIZER_ADDRESS void handler(int sig) {
  stack_size = save_context(thread_stack_bounds, regs, stack);
  stop = true;
}

DDPROF_NOINLINE void funcD() {
  {
    std::unique_lock lock{mutex};
    cv.notify_one();
  }
  while (!stop) {}
}

DDPROF_NOINLINE void funcC() {
  thread_stack_bounds = retrieve_stack_bounds();
  signal(SIGUSR1, &handler);
  defer { signal(SIGUSR1, SIG_DFL); };
  stop = false;

  funcD();
  DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
}

TEST(getcontext, unwind_from_sighandler) {
  LogHandle log_handle;
  std::unique_lock lock{mutex};
  std::thread t{funcC};
  cv.wait(lock);
  pthread_kill(t.native_handle(), SIGUSR1);
  t.join();

  UnwindState state = create_unwind_state().value();
  unwind_init_sample(&state, regs, getpid(), stack_size,
                     reinterpret_cast<char *>(stack));
  unwindstate_unwind(&state);

  auto &symbol_table = state.symbol_hdr._symbol_table;

  for (size_t iloc = 0; iloc < state.output.locs.size(); ++iloc) {
    auto &symbol = symbol_table[state.output.locs[iloc]._symbol_idx];
    printf("%zu: %s %lx \n", iloc, symbol._demangle_name.c_str(),
           state.output.locs[iloc].ip);
  }
  auto get_symbol = [&](int idx) {
    return symbol_table[state.output.locs[idx]._symbol_idx];
  };

  EXPECT_GT(state.output.locs.size(), 5);
  EXPECT_LT(state.output.locs.size(), 25);
  EXPECT_TRUE(
      get_symbol(0)._demangle_name.starts_with("ddprof::save_context("));
  EXPECT_EQ(get_symbol(1)._demangle_name, "ddprof::handler(int)");
  size_t next_idx = 3;
  while (next_idx < state.output.locs.size() - 1 &&
         get_symbol(next_idx)._demangle_name != "ddprof::funcD()") {
    ++next_idx;
  }
  EXPECT_EQ(get_symbol(next_idx)._demangle_name, "ddprof::funcD()");
  EXPECT_EQ(get_symbol(next_idx + 1)._demangle_name, "ddprof::funcC()");
}
#endif

} // namespace ddprof
