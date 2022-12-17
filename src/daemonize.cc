// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "daemonize.hpp"
#include "logger.hpp"

namespace ddprof {
bool DaemonizeResult::daemonize(std::function<void()> cleanup_function) {
  if (!openpipes()) {
    return false;
  }
  pid_t parent_pid = getpid();
  pid_t temp_pid = fork(); // "middle"/"child" (temporary) PID
  pid_t grandchild_pid = -1;
  const size_t pid_sz = sizeof(grandchild_pid);

  if (!temp_pid) { // temp PID enter branch
    close(pipe_read);
    pipe_read = -1;

    if ((grandchild_pid = fork())) { // temp PID enter branch
      if (cleanup_function) {
        cleanup_function();
      }

      // Child (temporary) PID closes
      std::exit(-1);

    } else { // grandchild (daemon) PID enter branch
      grandchild_pid = getpid();
      if (write(pipe_write, &grandchild_pid, pid_sz) != pid_sz) {
        std::exit(-1);
      }

      // Grandchild (target) PID returns
      state = DaemonizeState::Daemon;
      invoker_pid = parent_pid;
      daemon_pid = grandchild_pid;
      return true;
    }
  } else if (temp_pid != -1) { // parent PID enter branch
    close(pipe_write);
    pipe_write = -1;
    if (read(pipe_read, &grandchild_pid, pid_sz) != pid_sz) {
      return false;
    }

    signal(SIGCHLD, SIG_IGN);
    state = DaemonizeState::Invoker;
    invoker_pid = parent_pid;
    daemon_pid = grandchild_pid;
    return true;
  }

  // Should only arrive here if the first-level fork failed, but add a sink
  // to capture possible extraneous forks
  if (getpid() != parent_pid) {
    LG_WRN("Extraneous PID (%d) detected", getpid());
    std::exit(-1);
  }

  state = DaemonizeState::Failure;
  close(pipe_read);
  close(pipe_write);
  pipe_read = -1;
  pipe_write = -1;
  return false;
}
} // namespace ddprof
