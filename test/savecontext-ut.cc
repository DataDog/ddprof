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
#include "symbol_helper.hpp"
#include "unwind.hpp"

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
  blaze_symbolizer *symbolizer = blaze_symbolizer_new();
  defer { blaze_symbolizer_free(symbolizer); };

  UnwindState state = create_unwind_state().value();
  uint64_t regs[k_nb_registers_to_unwind];
  size_t stack_size = save_context(retrieve_stack_bounds(), regs, stack);

  unwind_init_sample(&state, regs, getpid(), stack_size,
                     reinterpret_cast<char *>(stack));
  unwindstate_unwind(&state);

  auto demangled_syms = collect_symbols(state, symbolizer);
  EXPECT_GT(demangled_syms.size(), 3);
  EXPECT_TRUE(demangled_syms[0].starts_with("ddprof::save_context("));
  EXPECT_EQ(demangled_syms[1], "ddprof::funcB()");
  EXPECT_EQ(demangled_syms[2], "ddprof::funcA()");
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
  blaze_symbolizer *symbolizer = blaze_symbolizer_new();
  defer { blaze_symbolizer_free(symbolizer); };
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
  auto demangled_syms = collect_symbols(state, symbolizer);
  EXPECT_GT(demangled_syms.size(), 5);
  EXPECT_LT(demangled_syms.size(), 25);
  EXPECT_TRUE(demangled_syms[0].starts_with("ddprof::save_context("));
  EXPECT_EQ(demangled_syms[1], "ddprof::handler(int)");
  size_t next_idx = 3;
  while (next_idx < state.output.locs.size() - 1 &&
         demangled_syms[next_idx] != "ddprof::funcD()") {
    ++next_idx;
  }
  EXPECT_EQ(demangled_syms[next_idx], "ddprof::funcD()");
  EXPECT_EQ(demangled_syms[next_idx + 1], "ddprof::funcC()");
}
#endif

} // namespace ddprof
