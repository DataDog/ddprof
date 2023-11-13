// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "daemonize.hpp"

#include "unique_fd.hpp"

#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ddprof {

namespace {
void handle_signal(int /*sig*/) {}
DaemonizeResult daemonize_error() {
  return {DaemonizeResult::Error, -1, -1, -1};
}
} // namespace

DaemonizeResult daemonize() {
  int pipefd[2];
  if (pipe2(pipefd, 0) == -1) {
    return daemonize_error();
  }

  UniqueFd readfd{pipefd[0]};
  UniqueFd writefd{pipefd[1]};

  const pid_t parent_pid = getpid();
  pid_t temp_pid = fork();

  if (temp_pid == -1) {
    return daemonize_error();
  }

  if (temp_pid == 0) { // Intermediate (temporary) process
    readfd.reset();

    temp_pid = getpid();
    pid_t child_pid = fork();
    if (child_pid != 0) { // Intermediate (temporary) process again

      struct sigaction sa;
      if (sigemptyset(&sa.sa_mask) == -1) {
        exit(1);
      }

      sa.sa_handler = &handle_signal;
      sa.sa_flags = 0;
      if (sigaction(SIGTERM, &sa, nullptr) == -1) {
        exit(1);
      }

      // Block until our child exits or sends us a SIGTERM signal.
      // In the happy path, child will send us a SIGTERM signal, that we catch
      // and then exit normally (to free resources and make valgrind happy).
      waitpid(child_pid, nullptr, 0);
      return {DaemonizeResult::IntermediateProcess, temp_pid, parent_pid,
              child_pid};
    }

    // Daemon process
    child_pid = getpid();
    if (write(writefd.get(), &child_pid, sizeof(child_pid)) !=
        sizeof(child_pid)) {
      exit(1);
    }
    return {DaemonizeResult::DaemonProcess, temp_pid, parent_pid, child_pid,
            std::move(writefd)};
  }

  // Initial process
  writefd.reset();

  pid_t grandchild_pid;
  if (read(readfd.get(), &grandchild_pid, sizeof(grandchild_pid)) !=
      sizeof(grandchild_pid)) {
    return daemonize_error();
  }

  // Wait until intermediate process terminates
  waitpid(temp_pid, nullptr, 0);
  return {DaemonizeResult::InitialProcess, temp_pid, parent_pid, grandchild_pid,
          std::move(readfd)};
}

} // namespace ddprof
