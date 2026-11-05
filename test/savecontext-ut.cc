// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "ddprof_base.hpp"
#include "defer.hpp"
#include "loghandle.hpp"
#include "perf.hpp"
#include "pthread_fixes.hpp"
#include "savecontext.hpp"
#include "symbol_helper.hpp"
#include "unwind.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <mutex>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
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
  // Same lower bound as glibc, also on musl whose pthread_getattr_np stops at
  // the mapped part of the stack.
  auto pthread_bounds = retrieve_stack_bounds();
  EXPECT_EQ(bounds.data(), pthread_bounds.data());
  EXPECT_GE(bounds.data() + bounds.size(),
            pthread_bounds.data() + pthread_bounds.size());
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

TEST(retrieve_stack_bounds_from_maps, preserves_errno_on_failure) {
  rlimit limit;
  ASSERT_EQ(getrlimit(RLIMIT_NOFILE, &limit), 0);
  auto exhausted_limit = limit;
  exhausted_limit.rlim_cur = 0;
  ASSERT_EQ(setrlimit(RLIMIT_NOFILE, &exhausted_limit), 0);
  std::span<const std::byte> bounds;
  int call_errno;
  {
    defer { setrlimit(RLIMIT_NOFILE, &limit); };
    errno = EDOM;
    bounds = retrieve_stack_bounds_from_maps();
    call_errno = errno;
  }
  EXPECT_TRUE(bounds.empty());
  EXPECT_EQ(call_errno, EDOM);
}

TEST(retrieve_stack_bounds_from_maps, not_a_cancellation_point) {
  std::atomic<bool> ready{false};
  std::atomic<bool> proceed{false};
  bool found_stack = false;
  std::thread t([&] {
    ready.store(true);
    while (!proceed.load()) {}
    // Allocation hooks are noexcept; cancellation must not unwind through them.
    const auto bounds = []() noexcept {
      return retrieve_stack_bounds_from_maps();
    }();
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr);
    found_stack = contains(bounds, __builtin_frame_address(0));
  });
  while (!ready.load()) {}
  const int cancel_result = pthread_cancel(t.native_handle());
  proceed.store(true);
  t.join();
  EXPECT_EQ(cancel_result, 0);
  EXPECT_TRUE(found_stack);
}

TEST(retrieve_stack_bounds_from_maps, secondary_thread) {
  std::thread t([] {
    errno = EDOM;
    auto maps_bounds = retrieve_stack_bounds_from_maps();
    EXPECT_EQ(errno, EDOM);
    auto pthread_bounds = retrieve_stack_bounds();
    EXPECT_TRUE(contains(maps_bounds, __builtin_frame_address(0)));
    ASSERT_FALSE(pthread_bounds.empty());
    EXPECT_LE(maps_bounds.data(), pthread_bounds.data());
    const auto frame = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    const auto self = reinterpret_cast<uintptr_t>(pthread_self());
    const auto end =
        reinterpret_cast<uintptr_t>(maps_bounds.data() + maps_bounds.size());
    if (frame < self && self <= end) {
      EXPECT_EQ(end, self);
    }
    uint64_t saved_regs[k_nb_registers_to_unwind];
    std::byte buffer[4096];
    EXPECT_GT(save_context(maps_bounds, saved_regs, buffer), 0);
  });
  t.join();
}

namespace {
std::span<const std::byte> raw_pthread_bounds() {
  pthread_attr_t attrs;
  if (pthread_getattr_np_safe(pthread_self(), &attrs) != 0) {
    return {};
  }
  defer { pthread_attr_destroy(&attrs); };
  void *address;
  size_t size;
  if (pthread_attr_getstack(&attrs, &address, &size) != 0) {
    return {};
  }
  return {static_cast<const std::byte *>(address), size};
}

constexpr uintptr_t kGrowthBytes = 64 * 1024;
std::array<std::byte, 8192> captured_stack;

DDPROF_NOINLINE int capture_after_growth(std::span<const std::byte> bounds,
                                         uintptr_t target, uintptr_t old_low) {
  const auto frame = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  if (frame >= target) {
    volatile char padding[4096];
    padding[0] = 1;
    padding[sizeof(padding) - 1] = 2;
    const int result = capture_after_growth(bounds, target, old_low);
    if (padding[0] != 1 || padding[sizeof(padding) - 1] != 2) {
      return 3;
    }
    DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
    return result;
  }
  std::array<uint64_t, k_perf_register_count> regs{};
  const auto size = save_context(bounds, regs, captured_stack);
  if (regs[REGNAME(SP)] >= old_low) {
    return 1;
  }
  return size > 0 ? 0 : 2;
}
} // namespace

TEST(retrieve_stack_bounds, main_stack_growth) {
  const auto raw_bounds = raw_pthread_bounds();
  const auto maps_bounds = retrieve_stack_bounds_from_maps();
  const auto bounds = retrieve_stack_bounds();
  ASSERT_FALSE(raw_bounds.empty());
  ASSERT_FALSE(maps_bounds.empty());
  ASSERT_FALSE(bounds.empty());
  EXPECT_EQ(bounds.data() + bounds.size(),
            raw_bounds.data() + raw_bounds.size());
  const auto raw_low = reinterpret_cast<uintptr_t>(raw_bounds.data());
  const auto maps_low = reinterpret_cast<uintptr_t>(maps_bounds.data());
  if (raw_low <= maps_low) {
    EXPECT_EQ(bounds.data(), raw_bounds.data());
    GTEST_SKIP() << "pthread bounds already account for main-stack growth";
  }
  if (raw_low - maps_low <= 2 * kGrowthBytes) {
    GTEST_SKIP() << "insufficient main-stack growth allowance";
  }
  ASSERT_EXIT(
      {
        const auto cached_bounds = retrieve_stack_bounds();
        const auto initial_bounds = raw_pthread_bounds();
        const auto initial_low =
            reinterpret_cast<uintptr_t>(initial_bounds.data());
        std::_Exit(capture_after_growth(
            cached_bounds, initial_low - kGrowthBytes, initial_low));
      },
      ::testing::ExitedWithCode(0), "");
}

TEST(retrieve_stack_bounds, preserves_forked_worker_stack) {
  // Make the VMA wider than the configured pthread stack.
  constexpr size_t kStackSize = 1024 * 1024;
  void *mapping = mmap(nullptr, 3 * kStackSize, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_NE(mapping, MAP_FAILED);
  defer { munmap(mapping, 3 * kStackSize); };
  pthread_attr_t attrs;
  ASSERT_EQ(pthread_attr_init(&attrs), 0);
  defer { pthread_attr_destroy(&attrs); };
  auto *stack_address = static_cast<std::byte *>(mapping) + kStackSize;
  ASSERT_EQ(pthread_attr_setstack(&attrs, stack_address, kStackSize), 0);

  int status = -1;
  pthread_t worker;
  ASSERT_EQ(pthread_create(
                &worker, &attrs,
                [](void *arg) -> void * {
                  auto *child_status = static_cast<int *>(arg);
                  const auto child = fork();
                  if (child == 0) {
                    const auto expected = raw_pthread_bounds();
                    const auto maps = retrieve_stack_bounds_from_maps();
                    const auto actual = retrieve_stack_bounds();
                    const bool leader = ::syscall(SYS_gettid) == getpid();
                    if (!leader || expected.empty() || maps.empty() ||
                        reinterpret_cast<uintptr_t>(maps.data()) >=
                            reinterpret_cast<uintptr_t>(expected.data())) {
                      std::_Exit(2);
                    }
                    std::_Exit(actual.data() == expected.data() &&
                                       actual.size() == expected.size()
                                   ? 0
                                   : 1);
                  }
                  if (child > 0) {
                    pid_t waited;
                    do {
                      waited = waitpid(child, child_status, 0);
                    } while (waited < 0 && errno == EINTR);
                    if (waited != child) {
                      *child_status = -1;
                    }
                  }
                  return nullptr;
                },
                &status),
            0);
  ASSERT_EQ(pthread_join(worker, nullptr), 0);
  ASSERT_GE(status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

} // namespace ddprof
