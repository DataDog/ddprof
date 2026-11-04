// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "defer.hpp"
#include "pthread_fixes.hpp"
#include "savecontext.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <gtest/gtest.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace ddprof {
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

TEST(retrieve_stack_bounds, retains_pthread_bounds_when_maps_unavailable) {
  rlimit limit;
  ASSERT_EQ(getrlimit(RLIMIT_NOFILE, &limit), 0);
  auto exhausted = limit;
  exhausted.rlim_cur = 0;
  std::span<const std::byte> expected;
  std::span<const std::byte> actual;
  int expected_errno;
  int actual_errno;
  {
    ASSERT_EQ(setrlimit(RLIMIT_NOFILE, &exhausted), 0);
    defer { setrlimit(RLIMIT_NOFILE, &limit); };
    errno = EDOM;
    expected = raw_pthread_bounds();
    expected_errno = errno;
    errno = EDOM;
    actual = retrieve_stack_bounds();
    actual_errno = errno;
  }
  if (expected.empty()) {
    EXPECT_TRUE(actual.empty());
    GTEST_SKIP() << "pthread discovery unavailable under descriptor exhaustion";
  }
  EXPECT_EQ(actual.data(), expected.data());
  EXPECT_EQ(actual.size(), expected.size());
  EXPECT_EQ(actual_errno, expected_errno);
}

TEST(retrieve_stack_bounds, preserves_secondary_pthread_bounds) {
  std::thread worker([] {
    const auto expected = raw_pthread_bounds();
    const auto actual = retrieve_stack_bounds();
    ASSERT_FALSE(expected.empty());
    EXPECT_EQ(actual.data(), expected.data());
    EXPECT_EQ(actual.size(), expected.size());
  });
  worker.join();
}

TEST(retrieve_stack_bounds, preserves_forked_worker_stack) {
  int status = -1;
  bool waited = false;
  std::thread worker([&] {
    const auto child = fork();
    if (child == 0) {
      const auto expected = raw_pthread_bounds();
      const auto actual = retrieve_stack_bounds();
      const bool leader = ::syscall(SYS_gettid) == getpid();
      std::_Exit(leader && !expected.empty() &&
                         actual.data() == expected.data() &&
                         actual.size() == expected.size()
                     ? 0
                     : 1);
    }
    if (child > 0) {
      pid_t result;
      do {
        result = waitpid(child, &status, 0);
      } while (result < 0 && errno == EINTR);
      waited = result == child;
    }
  });
  worker.join();
  ASSERT_TRUE(waited);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

} // namespace ddprof
