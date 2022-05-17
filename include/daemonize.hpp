// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <functional>
#include <sys/types.h>

namespace ddprof {

struct DaemonizeResult {
  pid_t
      temp_pid; // -1 on failure, 0 for initial process, > 0 for daemon process
  pid_t parent_pid; // pid of process initiating daemonize
  pid_t daemon_pid; // pid of daemon process
};

// Daemonization function
// cleanup_function is a callable invoked in the context of the intermediate,
// short-lived process that will be killed by daemon process.
DaemonizeResult daemonize(std::function<void()> cleanup_function = {});
} // namespace ddprof