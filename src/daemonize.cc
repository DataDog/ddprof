// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "daemonize.hpp"
#include "ddprof_exit.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ddprof {
DaemonizeResult daemonize(std::function<void()> cleanup_function) {
  int pipefd[2];
  if (pipe2(pipefd, O_CLOEXEC) == -1) {
    return {-1, -1, -1};
  }

  pid_t parent_pid = getpid();
  pid_t temp_pid = fork(); // "middle" (temporary) PID

  if (temp_pid == -1) {
    return {-1, -1, -1};
  }

  if (!temp_pid) { // If I'm the temp PID enter branch
    close(pipefd[0]);

    temp_pid = getpid();
    if (pid_t child_pid = fork();
        child_pid) { // If I'm the temp PID again, enter branch
      if (cleanup_function) {
        cleanup_function();
      }

      // Block until our child exits or sends us a kill signal
      // NOTE, current process is NOT expected to unblock here; rather it
      // ends by SIGTERM.  Exiting here is an error condition.
      waitpid(child_pid, NULL, 0);
      ddprof::exit();
    } else {
      child_pid = getpid();
      if (write(pipefd[1], &child_pid, sizeof(child_pid)) !=
          sizeof(child_pid)) {
        ddprof::exit();
      }
      close(pipefd[1]);
      // If I'm the child PID, then leave and attach profiler
      return {temp_pid, parent_pid, child_pid};
    }
  } else {
    close(pipefd[1]);

    pid_t grandchild_pid;
    if (read(pipefd[0], &grandchild_pid, sizeof(grandchild_pid)) !=
        sizeof(grandchild_pid)) {
      return {-1, -1, -1};
    }

    // If I'm the target PID, then now it's time to wait until my
    // child, the middle PID, returns.
    waitpid(temp_pid, NULL, 0);
    return {0, parent_pid, grandchild_pid};
  }

  return {-1, -1, -1};
}

} // namespace ddprof
