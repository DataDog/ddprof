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
#include "loghandle.hpp"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
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

// unwinding from signal handler does not work well on aarch64
#ifdef __x86_64__
static std::atomic<bool> stop;
static std::mutex mutex;
static std::condition_variable cv;
static uint64_t regs[K_NB_REGS_UNWIND];
static size_t stack_size;

DDPROF_NO_SANITIZER_ADDRESS void handler(int sig) {
  stack_size = save_context(regs, stack);
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

  UnwindState state;
  ddprof::unwind_init_sample(&state, regs, getpid(), stack_size,
                             reinterpret_cast<char *>(stack));
  ddprof::unwindstate__unwind(&state);

  auto &symbol_table = state.symbol_hdr._symbol_table;

  for (size_t iloc = 0; iloc < state.output.nb_locs; ++iloc) {
    auto &symbol = symbol_table[state.output.locs[iloc]._symbol_idx];
    printf("%zu: %s %lx \n", iloc, symbol._demangle_name.c_str(), state.output.locs[iloc].ip);
  }
  auto get_symbol = [&](int idx) {
    return symbol_table[state.output.locs[idx]._symbol_idx];
  };

  EXPECT_GT(state.output.nb_locs, 5);
  EXPECT_TRUE(get_symbol(0)._demangle_name.starts_with("save_context("));
  EXPECT_EQ(get_symbol(1)._demangle_name, "handler(int)");
  size_t next_idx = 3;
  while (next_idx < state.output.nb_locs - 1 &&
         get_symbol(next_idx)._demangle_name != "funcD()") {

    ++next_idx;
  }
  EXPECT_EQ(get_symbol(next_idx)._demangle_name, "funcD()");
  EXPECT_EQ(get_symbol(next_idx + 1)._demangle_name, "funcC()");
}
#endif

//562d7ac8a000-562d7aca3000 r--p 00000000 fe:01 3126187                    /app/build_gcc_alpine-linux_Rel/test/savecontext-ut
//562d7aca3000-562d7ad36000 r-xp 00019000 fe:01 3126187                    /app/build_gcc_alpine-linux_Rel/test/savecontext-ut
//562d7ad36000-562d7ad73000 r--p 000ac000 fe:01 3126187                    /app/build_gcc_alpine-linux_Rel/test/savecontext-ut
//562d7ad73000-562d7ad7e000 r--p 000e8000 fe:01 3126187                    /app/build_gcc_alpine-linux_Rel/test/savecontext-ut
//562d7ad7e000-562d7ad7f000 rw-p 000f3000 fe:01 3126187                    /app/build_gcc_alpine-linux_Rel/test/savecontext-ut
//562d7ad7f000-562d7ad87000 rw-p 00000000 00:00 0
//562d7af01000-562d7af02000 ---p 00000000 00:00 0                          [heap]
//562d7af02000-562d7af04000 rw-p 00000000 00:00 0                          [heap]
//7f15c8cc3000-7f15c8cc5000 ---p 00000000 00:00 0
//7f15c8cc5000-7f15c8cfd000 rw-p 00000000 00:00 0
//7f15c8cfd000-7f15c8d06000 r--p 00000000 fe:01 2503523                    /usr/lib/libgmock.so.1.11.0
//7f15c8d06000-7f15c8d0d000 r-xp 00009000 fe:01 2503523                    /usr/lib/libgmock.so.1.11.0
//7f15c8d0d000-7f15c8d11000 r--p 00010000 fe:01 2503523                    /usr/lib/libgmock.so.1.11.0
//7f15c8d11000-7f15c8d12000 r--p 00014000 fe:01 2503523                    /usr/lib/libgmock.so.1.11.0
//7f15c8d12000-7f15c8d13000 r--p 00014000 fe:01 2503523                    /usr/lib/libgmock.so.1.11.0
//7f15c8d13000-7f15c8d14000 rw-p 00015000 fe:01 2503523                    /usr/lib/libgmock.so.1.11.0
//7f15c8d14000-7f15c8d17000 r--p 00000000 fe:01 2503103                    /usr/lib/libgcc_s.so.1
//7f15c8d17000-7f15c8d28000 r-xp 00003000 fe:01 2503103                    /usr/lib/libgcc_s.so.1
//7f15c8d28000-7f15c8d2b000 r--p 00014000 fe:01 2503103                    /usr/lib/libgcc_s.so.1
//7f15c8d2b000-7f15c8d2c000 r--p 00016000 fe:01 2503103                    /usr/lib/libgcc_s.so.1
//7f15c8d2c000-7f15c8d2d000 rw-p 00017000 fe:01 2503103                    /usr/lib/libgcc_s.so.1
//7f15c8d2d000-7f15c8de7000 r--p 00000000 fe:01 2503105                    /usr/lib/libstdc++.so.6.0.29
//7f15c8de7000-7f15c8e8a000 r-xp 000ba000 fe:01 2503105                    /usr/lib/libstdc++.so.6.0.29
//7f15c8e8a000-7f15c8ef2000 r--p 0015d000 fe:01 2503105                    /usr/lib/libstdc++.so.6.0.29
//7f15c8ef2000-7f15c8f01000 r--p 001c4000 fe:01 2503105                    /usr/lib/libstdc++.so.6.0.29
//7f15c8f01000-7f15c8f02000 rw-p 001d3000 fe:01 2503105                    /usr/lib/libstdc++.so.6.0.29
//7f15c8f02000-7f15c8f05000 rw-p 00000000 00:00 0
//7f15c8f05000-7f15c8f08000 r--p 00000000 fe:01 1987144                    /lib/libz.so.1.2.12
//7f15c8f08000-7f15c8f16000 r-xp 00003000 fe:01 1987144                    /lib/libz.so.1.2.12
//7f15c8f16000-7f15c8f1d000 r--p 00011000 fe:01 1987144                    /lib/libz.so.1.2.12
//7f15c8f1d000-7f15c8f1e000 r--p 00017000 fe:01 1987144                    /lib/libz.so.1.2.12
