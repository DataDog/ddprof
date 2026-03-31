// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// Standalone test (no gtest) to verify TLS availability across fork().
// Gtest is avoided because forking inside gtest is fragile.

#include "lib/allocation_tracker.hpp"
#include "loghandle.hpp"

#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ddprof {
namespace {

#define CHECK_OR_RETURN(cond, ...)                                             \
  do {                                                                         \
    if (!(cond)) {                                                             \
      LG_ERR(__VA_ARGS__);                                                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)

#define CHECK_OR_EXIT(cond, ...)                                               \
  do {                                                                         \
    if (!(cond)) {                                                             \
      LG_ERR(__VA_ARGS__);                                                     \
      _exit(1);                                                                \
    }                                                                          \
  } while (0)

void *thread_check_null_tls(void *arg) {
  auto *result = static_cast<int *>(arg);
  auto *state = AllocationTracker::get_tl_state(false);
  if (state != nullptr) {
    LG_ERR("new thread expected NULL TLS, got %p", static_cast<void *>(state));
    *result = 1;
    return nullptr;
  }
  *result = 0;
  return nullptr;
}

int run_child(void *parent_state) {
  // After fork, the __thread buffer is inherited: the child's main thread
  // sees the initialized state at the same virtual address.
  auto *child_inherited = AllocationTracker::get_tl_state();
  CHECK_OR_EXIT(child_inherited == parent_state,
                "expected inherited TLS %p, got %p", parent_state,
                static_cast<void *>(child_inherited));

  // Re-init to get a fresh state for the child
  auto *child_state = AllocationTracker::init_tl_state();
  CHECK_OR_EXIT(child_state != nullptr, "init_tl_state() returned NULL");

  auto *retrieved = AllocationTracker::get_tl_state();
  CHECK_OR_EXIT(retrieved == child_state, "get/init mismatch: %p vs %p",
                static_cast<void *>(retrieved),
                static_cast<void *>(child_state));

  // A new thread in the child must start with NULL TLS (zero-initialized)
  pthread_t thread;
  int thread_result = -1;
  CHECK_OR_EXIT(pthread_create(&thread, nullptr, thread_check_null_tls,
                               &thread_result) == 0,
                "pthread_create failed");
  pthread_join(thread, nullptr);
  CHECK_OR_EXIT(thread_result == 0, "child thread TLS was not NULL");

  _exit(0);
}

} // namespace
} // namespace ddprof

int main() {
  using namespace ddprof;
  const LogHandle log_handle(LL_NOTICE);
  LG_NTC("allocation_tracker_fork_test starting");

  // Before any init, main thread's TLS must be zero-initialized by libc,
  // so get_tl_state() should return NULL (initialized == false).
  auto *pre_init = AllocationTracker::get_tl_state(false);
  CHECK_OR_RETURN(pre_init == nullptr,
                  "main thread TLS not zero-initialized before init (got %p)",
                  static_cast<void *>(pre_init));

  // Verify the same zero-initialization contract on a new thread
  {
    pthread_t thread;
    int thread_result = -1;
    CHECK_OR_RETURN(pthread_create(&thread, nullptr, thread_check_null_tls,
                                   &thread_result) == 0,
                    "pthread_create failed (pre-fork thread)");
    pthread_join(thread, nullptr);
    CHECK_OR_RETURN(thread_result == 0,
                    "pre-fork thread TLS was not zero-initialized");
  }

  // Create TLS in parent
  auto *parent_state = AllocationTracker::init_tl_state();
  CHECK_OR_RETURN(parent_state != nullptr,
                  "parent init_tl_state() returned NULL");

  auto *retrieved = AllocationTracker::get_tl_state();
  CHECK_OR_RETURN(retrieved == parent_state, "parent get/init mismatch");

  fflush(stdout);
  fflush(stderr);

  const pid_t pid = fork();
  CHECK_OR_RETURN(pid != -1, "fork failed: %s", strerror(errno));

  if (pid == 0) {
    run_child(parent_state);
    _exit(0);
  }

  // Parent: wait for child
  int status;
  waitpid(pid, &status, 0);

  if (!WIFEXITED(status)) {
    if (WIFSIGNALED(status)) {
      LG_ERR("child killed by signal %d", WTERMSIG(status));
    } else {
      LG_ERR("child did not exit normally");
    }
    return 1;
  }

  const int exit_code = WEXITSTATUS(status);
  CHECK_OR_RETURN(exit_code == 0, "child exited with code %d", exit_code);

  // Parent TLS should be unaffected by the fork
  auto *parent_after = AllocationTracker::get_tl_state();
  CHECK_OR_RETURN(parent_after == parent_state,
                  "parent TLS corrupted after fork (%p vs %p)",
                  static_cast<void *>(parent_after),
                  static_cast<void *>(parent_state));

  LG_NTC("allocation_tracker_fork_test passed");
  return 0;
}
