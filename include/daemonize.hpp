// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "unique_fd.hpp"

#include <sys/types.h>

namespace ddprof {

struct DaemonizeResult {
  enum State : uint8_t {
    Error,
    InitialProcess,
    IntermediateProcess,
    DaemonProcess
  };
  State state; // Only InitialProcess can return in a Failure state
  pid_t
      temp_pid; // -1 on failure, 0 for initial process, > 0 for daemon process
  pid_t parent_pid; // pid of process initiating daemonize
  pid_t daemon_pid; // pid of daemon process
  UniqueFd
      pipe_fd; // pipe to communicate from daemon process to initial process
};

// Daemonization function
// short-lived process that will be killed by daemon process.
DaemonizeResult daemonize();
} // namespace ddprof