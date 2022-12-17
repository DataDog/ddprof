// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <fcntl.h>
#include <functional>
#include <poll.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <unistd.h>

namespace ddprof {

enum class DaemonizeState { Failure = -1, Invoker = 0, Daemon = 1 };

struct DaemonizeResult {
  DaemonizeState state = DaemonizeState::Failure;
  pid_t invoker_pid = -1;
  pid_t daemon_pid = -1;

  void finalize() {
    switch (state) {
    case DaemonizeState::Failure:
      break;
    case DaemonizeState::Daemon:
      signal(SIGPIPE, SIG_IGN);
      write(pipe_write, &daemon_pid, sizeof(pid_t));
      signal(SIGPIPE, SIG_DFL);
      close(pipe_write);
      pipe_write = 1;
      break;
    case DaemonizeState::Invoker: {
      struct pollfd pfd = {pipe_read, POLLIN};
      poll(&pfd, 1, 1000); // wait for 1s or until write/signal
      signal(SIGCHLD, SIG_IGN);
    }
      close(pipe_read);
      pipe_read = -1;
      break;
    }
  };

  bool is_failure() { return state == DaemonizeState::Failure; }
  bool is_daemon() { return state == DaemonizeState::Daemon; }
  bool is_invoker() { return state == DaemonizeState::Invoker; }

  // Daemonization function
  // cleanup_function is a callable invoked in the context of the intermediate,
  // short-lived process that will be killed by daemon process.
  bool daemonize(std::function<void()> cleanup_function);

private:
  int pipe_read = -1;
  int pipe_write = -1;

  bool openpipes() {
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) == -1) {
      return false;
    }
    pipe_read = pipefd[0];
    pipe_write = pipefd[1];
    return true;
  };
};

DaemonizeResult daemonize(std::function<void()> cleanup_function = {});
} // namespace ddprof
