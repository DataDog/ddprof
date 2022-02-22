// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_wrapper.h"

extern "C" {
#include "ddprof_cmdline.h"
#include "logger_setup.h"
}
#include "defer.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

static constexpr int k_default_timeout_ms = 1000;

extern const char _binary_ddprof_start[];
extern const char _binary_ddprof_end[];

namespace {
struct ProfilerState {
  bool started = false;
  pid_t profiler_pid = 0;
};

ProfilerState g_state;

struct ProfilerAutoStart {
  ProfilerAutoStart() {
    // Note that library needs to be linked with `--no-as-needed` when using
    // autostart Otherwise linker will completely remove library from DT_NEEDED
    // and library will not be loaded

    bool autostart = false;
    const char *autostart_env = getenv("DD_PROFILING_NATIVE_AUTOSTART");
    if (autostart_env && arg_yesno(autostart_env, 1)) {
      autostart = true;
    } else {
      // if library is preloaded, autostart profiling since there is no way
      // otherwise to start profiling
      const char *ldpreload_env = getenv("LD_PRELOAD");
      if (ldpreload_env && strstr(ldpreload_env, "libddprof_wrapper.so")) {
        autostart = true;
      }
    }
    if (autostart) {
      ddprof_start_profiling();
    }
  }

  ~ProfilerAutoStart() {
    if (g_state.started) {
      ddprof_stop_profiling(k_default_timeout_ms);
    }
  }
};

ProfilerAutoStart g_autostart;
} // namespace

int ddprof_start_profiling() {
  if (g_state.started) {
    return -1;
  }
  pid_t target_pid = getpid();
  int pipefd[2];
  if (pipe2(pipefd, O_CLOEXEC) == -1) {
    return -1;
  }

  pid_t middle_pid = fork();
  if (middle_pid == -1) {
    return -1;
  }

  if (middle_pid == 0) {
    // executed in middle
    close(pipefd[0]);

    auto defer_pipeclose = make_defer([&pipefd] { close(pipefd[1]); });

    middle_pid = getpid();
    if (middle_pid == -1) {
      return -1;
    }

    pid_t grandchild_pid = fork();
    if (grandchild_pid == 0) {
      // executed in grand child
      grandchild_pid = getpid();
      char ddprof_str[] = "ddprof";

      int fd = syscall(SYS_memfd_create, ddprof_str, 1U /*MFD_CLOEXEC*/);

      if (fd == -1) {
        return -1;
      }
      defer { close(fd); };

      if (write(fd, _binary_ddprof_start,
                // cppcheck-suppress comparePointers
                _binary_ddprof_end - _binary_ddprof_start) == -1) {
        return -1;
      }

      char pid_buf[32];
      snprintf(pid_buf, sizeof(pid_buf), "%d", target_pid);

      kill(middle_pid, SIGTERM);
      if (write(pipefd[1], &grandchild_pid, sizeof(grandchild_pid)) !=
          sizeof(grandchild_pid)) {
        return -1;
      }

      defer_pipeclose.release();
      close(pipefd[1]);

      char pid_opt_str[] = "-p";
      char *argv[] = {ddprof_str, pid_opt_str, pid_buf, NULL};

      // unset LD_PRELOAD, otherwise if libddprof_wrapper.so was preloaded, it
      // would trigger a fork bomb
      unsetenv("LD_PRELOAD");

      if (fexecve(fd, argv, environ) == -1) {
        return -1;
      }
    } else {
      // executed in middle
      defer_pipeclose.release();
      close(pipefd[1]);

      // waiting to be killed by grand child
      waitpid(grandchild_pid, NULL, 0);

      // should never be executed
      return -1;
    }
  } else {
    // executed by parent
    close(pipefd[1]);
    defer { close(pipefd[0]); };

    waitpid(middle_pid, NULL, 0);
    pid_t grandchild_pid;
    if (read(pipefd[0], &grandchild_pid, sizeof(grandchild_pid)) !=
        sizeof(grandchild_pid)) {
      return -1;
    }
    g_state.profiler_pid = grandchild_pid;
    g_state.started = true;
  }

  return 0;
}

void ddprof_stop_profiling(int timeout_ms) {
  if (!g_state.started) {
    return;
  }

  auto time_limit =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  kill(g_state.profiler_pid, SIGTERM);
  const auto time_slice = std::chrono::milliseconds(10);

  while (std::chrono::steady_clock::now() < time_limit) {
    if (kill(g_state.profiler_pid, 0) == -1 && errno == ESRCH) {
      break;
    }
    std::this_thread::sleep_for(time_slice);
  }

  g_state.started = false;
}
