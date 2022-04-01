// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dd_profiling.h"

extern "C" {
#include "ddprof_cmdline.h"
#include "logger_setup.h"
}
#include "defer.hpp"
#include "ipc.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

static constexpr const char *const kProfilerActiveEnvVariable =
    "DDPDD_PROFILING_NATIVE_LIBRARY_ACTIVE";
static constexpr const char *const kProfilerAutoStartEnvVariable =
    "DD_PROFILING_NATIVE_AUTOSTART";

extern const char _binary_ddprof_start[]; // NOLINT cert-dcl51-cpp
extern const char _binary_ddprof_end[];   // NOLINT cert-dcl51-cpp

namespace {
struct ProfilerState {
  bool started = false;
  pid_t profiler_pid = 0;
};

ProfilerState g_state;

// return true if this profiler is active for this process or one of its parent
bool is_profiler_library_active() {
  return getenv(kProfilerActiveEnvVariable) != nullptr;
}

void set_profiler_library_active() {
  setenv(kProfilerActiveEnvVariable, "1", 1);
}

void set_profiler_library_inactive() { unsetenv(kProfilerActiveEnvVariable); }

struct ProfilerAutoStart {
  ProfilerAutoStart() noexcept {
    // Note that library needs to be linked with `--no-as-needed` when using
    // autostart, otherwise linker will completely remove library from DT_NEEDED
    // and library will not be loaded.

    bool autostart = false;
    const char *autostart_env = getenv(kProfilerAutoStartEnvVariable);
    if (autostart_env && arg_yesno(autostart_env, 1)) {
      autostart = true;
    } else {
      // if library is preloaded, autostart profiling since there is no way
      // otherwise to start profiling
      const char *ldpreload_env = getenv("LD_PRELOAD");
      if (ldpreload_env && strstr(ldpreload_env, "libdd_profiling.so")) {
        autostart = true;
      }
    }
    if (autostart) {
      ddprof_start_profiling();
    }
  }

  ~ProfilerAutoStart() {
    // profiler will stop when process exits
  }
};

ProfilerAutoStart g_autostart;
} // namespace

static int ddprof_start_profiling_internal() {
  // Refuse to start profiler if already started by this process or if active if
  // one of its parent
  if (g_state.started || is_profiler_library_active()) {
    return -1;
  }
  pid_t target_pid = getpid();
  int sockfds[2];
  if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sockfds) == -1) {
    return -1;
  }

  pid_t middle_pid = fork();
  if (middle_pid == -1) {
    return -1;
  }

  if (middle_pid == 0) {
    // executed in middle
    close(sockfds[0]);

    auto defer_socketclose = make_defer([&sockfds] { close(sockfds[1]); });

    middle_pid = getpid();
    if (middle_pid == -1) {
      return -1;
    }

    pid_t grandchild_pid = fork();
    if (grandchild_pid == 0) {
      // executed in grand child
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
      char sock_buf[32];
      snprintf(sock_buf, sizeof(sock_buf), "%d", sockfds[1]);

      char pid_opt_str[] = "-p";
      char sock_opt_str[] = "-Z";

      char *argv[] = {ddprof_str,   pid_opt_str, pid_buf,
                      sock_opt_str, sock_buf,    NULL};

      // unset LD_PRELOAD, otherwise if libdd_profiling.so was preloaded, it
      // would trigger a fork bomb
      unsetenv("LD_PRELOAD");

      kill(middle_pid, SIGTERM);

      if (fexecve(fd, argv, environ) == -1) {
        return -1;
      }
    } else {
      // executed in middle
      defer_socketclose.reset();

      // waiting to be killed by grand child
      waitpid(grandchild_pid, NULL, 0);

      // should never be executed
      return -1;
    }
  }

  // executed by parent
  close(sockfds[1]);
  defer { close(sockfds[0]); };

  waitpid(middle_pid, NULL, 0);

  ddprof::RequestMessage req = {.request = ddprof::RequestMessage::kPid};
  if (!ddprof::send(sockfds[0], req)) {
    return -1;
  }
  ddprof::ResponseMessage resp;
  if (!ddprof::receive(sockfds[0], resp)) {
    return -1;
  }

  set_profiler_library_active();
  g_state.profiler_pid = resp.data.pid;
  g_state.started = true;

  return 0;
}

int ddprof_start_profiling() {
  try {
    return ddprof_start_profiling_internal();
  } catch (...) {}
  return -1;
}

void ddprof_stop_profiling(int timeout_ms) {
  if (!g_state.started) {
    return;
  }

  defer {
    g_state.started = false;
    set_profiler_library_inactive();
  };

  auto time_limit =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  kill(g_state.profiler_pid, SIGTERM);
  const auto time_slice = std::chrono::milliseconds(10);

  while (std::chrono::steady_clock::now() < time_limit) {
    std::this_thread::sleep_for(time_slice);

    // check if profiler process is still alive
    if (kill(g_state.profiler_pid, 0) == -1 && errno == ESRCH) {
      return;
    }
  }

  // timeout reached and profiler process is still not dead
  // Do a forceful kill
  kill(g_state.profiler_pid, SIGKILL);
}
