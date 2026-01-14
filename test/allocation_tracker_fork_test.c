// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// Simple test to verify pthread key initialization works across fork()
// This tests the atomic-based pthread_once replacement

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Forward declare the C++ functions we need
#ifdef __cplusplus
extern "C" {
#endif

void *AllocationTracker_get_tl_state(void);
void *AllocationTracker_init_tl_state(void);

#ifdef __cplusplus
}
#endif

// Thread function to test TLS in new thread
static void *thread_test_func(void *arg) {
  int *result = (int *)arg;

  void *thread_state = AllocationTracker_get_tl_state();

  if (thread_state != NULL) {
    fprintf(stderr, "FAIL: New thread expected NULL TLS, got %p\n",
            thread_state);
    *result = 1;
    return NULL;
  }

  *result = 0;
  return NULL;
}

int main(void) {
  // Initialize pthread key and create TLS data in parent
  void *state_before = AllocationTracker_get_tl_state();

  void *parent_state = AllocationTracker_init_tl_state();
  if (parent_state == NULL) {
    fprintf(stderr, "FAIL: Parent init_tl_state() returned NULL\n");
    return 1;
  }

  void *retrieved = AllocationTracker_get_tl_state();
  if (retrieved != parent_state) {
    fprintf(stderr, "FAIL: Parent get_tl_state() mismatch\n");
    return 1;
  }

  // Test fork - this is where the old pthread_once bug would crash
  fflush(stdout);
  fflush(stderr);

  pid_t pid = fork();
  if (pid == -1) {
    perror("FAIL: fork");
    return 1;
  }

  if (pid == 0) {
    // Child process - test pthread key works after fork
    void *child_inherited = AllocationTracker_get_tl_state();

    void *child_state = AllocationTracker_init_tl_state();
    if (child_state == NULL) {
      fprintf(stderr, "FAIL: Child init_tl_state() returned NULL\n");
      _exit(1);
    }

    void *child_retrieved = AllocationTracker_get_tl_state();
    if (child_retrieved != child_state) {
      fprintf(stderr, "FAIL: Child pthread key broken after fork\n");
      _exit(2);
    }

    // Test that a new thread starts with NULL TLS
    pthread_t thread;
    int thread_result = -1;
    if (pthread_create(&thread, NULL, thread_test_func, &thread_result) != 0) {
      fprintf(stderr, "FAIL: pthread_create failed\n");
      _exit(3);
    }

    pthread_join(thread, NULL);
    if (thread_result != 0) {
      _exit(4);
    }

    _exit(0);
  }

  // Parent waits for child
  int status;
  waitpid(pid, &status, 0);

  if (!WIFEXITED(status)) {
    if (WIFSIGNALED(status)) {
      fprintf(stderr,
              "FAIL: Child crashed with signal %d (pthread_once ABI bug!)\n",
              WTERMSIG(status));
    } else {
      fprintf(stderr, "FAIL: Child did not exit normally\n");
    }
    return 1;
  }

  int exit_code = WEXITSTATUS(status);
  if (exit_code != 0) {
    const char *reason = "unknown";
    if (exit_code == 1)
      reason = "init_tl_state failed";
    else if (exit_code == 2)
      reason = "pthread key broken";
    else if (exit_code == 3)
      reason = "pthread_create failed";
    else if (exit_code == 4)
      reason = "thread test failed";
    fprintf(stderr, "FAIL: Child exited with code %d (%s)\n", exit_code,
            reason);
    return 1;
  }

  // Verify parent still works after fork
  void *parent_after = AllocationTracker_get_tl_state();
  if (parent_after != parent_state) {
    fprintf(stderr, "FAIL: Parent TLS corrupted after fork\n");
    return 1;
  }

  // All tests passed - silent success for CI
  return 0;
}
