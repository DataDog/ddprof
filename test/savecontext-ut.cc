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
#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <mutex>
#include <sys/mman.h>
#include <sys/resource.h>
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

namespace {
bool contains(std::span<const std::byte> bounds, const void *addr) {
  const auto *p = static_cast<const std::byte *>(addr);
  return !bounds.empty() && bounds.data() <= p &&
      p < bounds.data() + bounds.size();
}
} // namespace

// __builtin_frame_address is used instead of the address of a local, which
// ASAN's fake stack mode can move to the heap.

TEST(retrieve_stack_bounds_from_maps, main_thread) {
  auto bounds = retrieve_stack_bounds_from_maps();
  EXPECT_TRUE(contains(bounds, __builtin_frame_address(0)));

  rlimit stack_limit;
  ASSERT_EQ(getrlimit(RLIMIT_STACK, &stack_limit), 0);
  if (stack_limit.rlim_cur != RLIM_INFINITY) {
    EXPECT_GE(bounds.size(), stack_limit.rlim_cur);
  }
#ifdef __GLIBC__
  // Same lower bound as glibc: rlimit size, capped by the mapping below. glibc
  // places the upper bound at the page of __libc_stack_end, within the mapping.
  auto pthread_bounds = retrieve_stack_bounds();
  EXPECT_EQ(bounds.data(), pthread_bounds.data());
  EXPECT_GE(bounds.data() + bounds.size(),
            pthread_bounds.data() + pthread_bounds.size());
#endif
}

namespace {
std::span<const std::byte> g_signal_stack_bounds;

void record_bounds_handler(int /*sig*/) {
  g_signal_stack_bounds = retrieve_stack_bounds_from_maps();
}
} // namespace

TEST(retrieve_stack_bounds_from_maps, main_thread_signal_stack) {
  // Alternate stack between two PROT_NONE guard pages, so its mapping cannot
  // be merged with neighbours. On it, the main thread rlimit extension must not
  // apply.
  const auto page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  const size_t stack_size = 64 * page;
  void *region = mmap(nullptr, stack_size + 2 * page, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_NE(region, MAP_FAILED);
  defer { munmap(region, stack_size + 2 * page); };
  auto *stack = static_cast<std::byte *>(region) + page;
  ASSERT_EQ(mprotect(stack, stack_size, PROT_READ | PROT_WRITE), 0);

  stack_t alt{};
  alt.ss_sp = stack;
  alt.ss_size = stack_size;
  stack_t old_alt{};
  ASSERT_EQ(sigaltstack(&alt, &old_alt), 0);
  defer { sigaltstack(&old_alt, nullptr); };

  struct sigaction action{};
  action.sa_handler = record_bounds_handler;
  action.sa_flags = SA_ONSTACK;
  struct sigaction old_action{};
  ASSERT_EQ(sigaction(SIGUSR1, &action, &old_action), 0);
  defer { sigaction(SIGUSR1, &old_action, nullptr); };

  ASSERT_EQ(raise(SIGUSR1), 0);
  EXPECT_EQ(g_signal_stack_bounds.data(), stack);
  EXPECT_EQ(g_signal_stack_bounds.size(), stack_size);
}

TEST(retrieve_stack_bounds_from_maps, preserves_errno) {
  errno = ENOMEM;
  auto bounds = retrieve_stack_bounds_from_maps();
  EXPECT_FALSE(bounds.empty());
  EXPECT_EQ(errno, ENOMEM);
}

TEST(retrieve_stack_bounds_from_maps, secondary_thread) {
  std::thread t([] {
    auto maps_bounds = retrieve_stack_bounds_from_maps();
    auto pthread_bounds = retrieve_stack_bounds();
    EXPECT_TRUE(contains(maps_bounds, __builtin_frame_address(0)));
    ASSERT_FALSE(pthread_bounds.empty());
    // The mapping holding the thread stack also holds its TCB/static TLS, so
    // it encloses the pthread-reported stack.
    EXPECT_LE(maps_bounds.data(), pthread_bounds.data());
    EXPECT_GE(maps_bounds.data() + maps_bounds.size(),
              pthread_bounds.data() + pthread_bounds.size());
  });
  t.join();
}

} // namespace ddprof
