// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "atomic_shared.hpp"

#include <functional>
#include <sys/types.h>
#include <unistd.h>

#include "daemonize.hpp"
#include "ddprof_exit.hpp"
#include "logger.hpp"

namespace ddprof {

enum class DaemonizeState { Failure = -1, Invoker = 0, Daemon = 1 };

struct DaemonizeResult {
  DaemonizeState state = DaemonizeState::Failure;
  pid_t invoker_pid = -1;
  pid_t daemon_pid = -1;

  // A reusable barrier between parent/daemon
  void barrier() {
    switch (state) {
    case DaemonizeState::Failure:
      break;
    case DaemonizeState::Daemon:
      sem_daemon->store(true);
      sem_invoker->value_timedwait(false, timeout_val);
      sem_invoker->store(false);
      break;
    case DaemonizeState::Invoker:
      sem_invoker->store(true);
      sem_daemon->value_timedwait(false, timeout_val);
      sem_daemon->store(false);
      break;
    }
  };

  bool is_failure() { return state == DaemonizeState::Failure; }
  bool is_daemon() { return state == DaemonizeState::Daemon; }
  bool is_invoker() { return state == DaemonizeState::Invoker; }

  // Daemonization function
  // cleanup_function is a callable invoked in the context of the intermediate,
  // short-lived process that will be killed by daemon process.
  bool daemonize(std::function<void()> cleanup_function = {}) {
    // Initialize
    pid_transfer = std::make_unique<AtomicShared<pid_t>>();
    sem_invoker = std::make_unique<AtomicShared<bool>>();
    sem_daemon = std::make_unique<AtomicShared<bool>>();
    pid_transfer->store(0);
    sem_invoker->store(false);
    sem_daemon->store(false);

    // Start daemonizing
    invoker_pid = getpid();
    pid_t temp_pid = fork(); // "middle"/"child" (temporary) PID
    state = DaemonizeState::Failure;

    if (!temp_pid) { // temp PID enter branch
      if (fork()) {  // temp PID enter branch
        if (cleanup_function) {
          cleanup_function();
        }

        // Temporary PID exits to force re-init of grandchild (daemon)
        throw ddprof::exit();
        std::exit(0);

      } else { // grandchild (daemon) PID enter branch
        daemon_pid = getpid();

        // Tell the invoker my PID, then wait until it gets changed again.
        pid_transfer->store(daemon_pid);
        if (!pid_transfer->value_timedwait(daemon_pid, timeout_val)) {
          return false;
        }
        state = DaemonizeState::Daemon;
        return true;
      }
    } else if (temp_pid != -1) { // parent PID enter branch
      // Try to read the PID of the daemon from shared memory, then notify
      if (!pid_transfer->value_timedwait(0, timeout_val)) {
        return false;
      }
      daemon_pid = pid_transfer->exchange(0); // consume + reset
      state = DaemonizeState::Invoker;
      return true;
    }

    // Should only arrive here if the first-level fork failed, but add sink to
    if (getpid() != invoker_pid) {
      LG_WRN("Extraneous PID (%d) detected", getpid());
      throw ddprof::exit();
      std::exit(-1);
    }
    return false;
  }

private:
  std::unique_ptr<AtomicShared<pid_t>> pid_transfer;
  std::unique_ptr<AtomicShared<bool>> sem_invoker;
  std::unique_ptr<AtomicShared<bool>> sem_daemon;
  static constexpr size_t timeout_val = 1000; // 10 seconds
};
} // namespace ddprof
