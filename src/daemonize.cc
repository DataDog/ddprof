// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "daemonize.hpp"

#include <cstdlib>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ddprof {

namespace {
void handle_signal(int) {}
DaemonizeResult daemonize_error() {
  return {DaemonizeResult::Error, -1, -1, -1};
}
} // namespace

DaemonizeResult daemonize() {
  int pipefd[2];
  if (pipe2(pipefd, O_CLOEXEC) == -1) {
    return daemonize_error();
  }

  pid_t parent_pid = getpid();
  pid_t temp_pid = fork(); // "middle" (temporary) PID

  if (temp_pid == -1) {
    return daemonize_error();
  }

  if (!temp_pid) { // If I'm the temp PID enter branch
    close(pipefd[0]);

    temp_pid = getpid();
    if (pid_t child_pid = fork();
        child_pid) { // If I'm the temp PID again, enter branch

      struct sigaction sa;
      if (sigemptyset(&sa.sa_mask) == -1) {
        exit(1);
      }

      sa.sa_handler = &handle_signal;
      sa.sa_flags = 0;
      if (sigaction(SIGTERM, &sa, NULL) == -1) {
        exit(1);
      }

      // Block until our child exits or sends us a kill signal
      // NOTE, current process is NOT expected to unblock here; rather it
      // ends by SIGTERM.
      waitpid(child_pid, NULL, 0);
      return {DaemonizeResult::IntermediateProcess, temp_pid, parent_pid,
              child_pid};
    } else {
      child_pid = getpid();
      if (write(pipefd[1], &child_pid, sizeof(child_pid)) !=
          sizeof(child_pid)) {
        exit(1);
      }
      close(pipefd[1]);
      // If I'm the child PID, then leave and attach profiler
      return {DaemonizeResult::DaemonProcess, temp_pid, parent_pid, child_pid};
    }
  } else {
    close(pipefd[1]);

    pid_t grandchild_pid;
    if (read(pipefd[0], &grandchild_pid, sizeof(grandchild_pid)) !=
        sizeof(grandchild_pid)) {
      return daemonize_error();
    }

    // If I'm the target PID, then now it's time to wait until my
    // child, the middle PID, returns.
    waitpid(temp_pid, NULL, 0);
    return {DaemonizeResult::InitialProcess, temp_pid, parent_pid,
            grandchild_pid};
  }
}

} // namespace ddprof