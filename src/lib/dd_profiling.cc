// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dd_profiling.h"

#include "allocation_tracker.hpp"

extern "C" {
#include "ddprof_cmdline.h"
#include "logger_setup.h"
}
#include "defer.hpp"
#include "ipc.hpp"
#include "syscalls.hpp"

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

static constexpr const char *k_profiler_active_env_variable =
    "DD_PROFILING_NATIVE_LIBRARY_ACTIVE";
static constexpr const char *k_profiler_auto_start_env_variable =
    "DD_PROFILING_NATIVE_AUTOSTART";
static constexpr const char *k_profiler_ddprof_exe_env_variable =
    "DD_PROFILING_NATIVE_DDPROF_EXE";

// address of embedded ddprof executable
extern const char _binary_ddprof_start[]; // NOLINT cert-dcl51-cpp
extern const char _binary_ddprof_end[];   // NOLINT cert-dcl51-cpp

namespace {
struct ProfilerState {
  bool started = false;
  bool allocation_profiling_started = false;
  pid_t profiler_pid = 0;
};

ProfilerState g_state;

// return true if this profiler is active for this process or one of its parent
bool is_profiler_library_active() {
  return getenv(k_profiler_active_env_variable) != nullptr;
}

void set_profiler_library_active() {
  setenv(k_profiler_active_env_variable, "1", 1);
}

void set_profiler_library_inactive() {
  unsetenv(k_profiler_active_env_variable);
}

void allocation_profiling_stop() {
  if (g_state.allocation_profiling_started) {
    ddprof::AllocationTracker::allocation_tracking_free();
    g_state.allocation_profiling_started = false;
  }
}

struct ProfilerAutoStart {
  ProfilerAutoStart() noexcept {
    // Note that library needs to be linked with `--no-as-needed` when using
    // autostart, otherwise linker will completely remove library from DT_NEEDED
    // and library will not be loaded.

    bool autostart = false;
    const char *autostart_env = getenv(k_profiler_auto_start_env_variable);
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

static int exec_ddprof(pid_t target_pid, pid_t parent_pid, int sock_fd) {
  char ddprof_str[] = "ddprof";

  char pid_buf[32];
  snprintf(pid_buf, sizeof(pid_buf), "%d", target_pid);
  char sock_buf[32];
  snprintf(sock_buf, sizeof(sock_buf), "%d", sock_fd);

  char pid_opt_str[] = "-p";
  char sock_opt_str[] = "-Z";

  // cppcheck-suppress variableScope
  char *argv[] = {ddprof_str,   pid_opt_str, pid_buf,
                  sock_opt_str, sock_buf,    NULL};

  // unset LD_PRELOAD, otherwise if libdd_profiling.so was preloaded, it
  // would trigger a fork bomb
  unsetenv("LD_PRELOAD");

  kill(parent_pid, SIGTERM);

  if (const char *ddprof_exe = getenv(k_profiler_ddprof_exe_env_variable);
      ddprof_exe) {
    execve(ddprof_exe, argv, environ);
  } else {
    int fd = ddprof::memfd_create(ddprof_str, 1U /*MFD_CLOEXEC*/);

    if (fd == -1) {
      return -1;
    }
    defer { close(fd); };

    if (write(fd, _binary_ddprof_start,
              // cppcheck-suppress comparePointers
              _binary_ddprof_end - _binary_ddprof_start) == -1) {
      return -1;
    }
    fexecve(fd, argv, environ);
  }

  return -1;
}

static int ddprof_start_profiling_internal() {
  // Refuse to start profiler if already started by this process or if active if
  // one of its parent
  if (g_state.started || is_profiler_library_active()) {
    return -1;
  }
  pid_t target_pid = getpid();
  enum { kParentIdx, kChildIdx };

  // Create a socket pair to establish a commuication channel between library
  // and profiler Communication occurs only during profiler setup and sockets
  // are closed once profiler is attached.
  // This channel is used to:
  //  * ensure that profiler is attached before returning
  //    from ddprof_start_profiling
  //  * retrieve PID of profiler on library side
  //  * retrieve ring buffer information for allocation profiling on library
  //    side. This requires passing file descriptors between procressers, that's
  //    why unix socket are used here.
  // Overview of the communication process:
  //  * library create socket pair and fork + exec into ddprof executable
  //  * library sends a message requesting profiler PID and waits for a reply
  //  * ddprof starts up, attaches itself to the target process, then waits for
  //    a PID request and replies
  //  * both library and profiler close their socket and continue
  int sockfds[2];
  if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sockfds) == -1) {
    return -1;
  }

  pid_t middle_pid = fork();
  if (middle_pid == -1) {
    return -1;
  }

  if (middle_pid == 0) {
    // executed in temporary intermediate process
    close(sockfds[kParentIdx]);

    auto defer_socketclose =
        make_defer([&sockfds] { close(sockfds[kChildIdx]); });

    middle_pid = getpid();
    if (middle_pid == -1) {
      return -1;
    }

    pid_t grandchild_pid = fork();
    if (grandchild_pid == 0) {
      // executed in grand child (ie. ddprof process)

      // exec_ddprof does not return on success
      exec_ddprof(target_pid, middle_pid, sockfds[kChildIdx]);
    } else {
      // executed in intermediate process
      defer_socketclose.reset();

      // waiting to be killed by grand child (ie. ddprof process)
      waitpid(grandchild_pid, NULL, 0);
    }

    // should never be executed
    exit(1);
  }

  // executed by parent process

  // close child socket end
  close(sockfds[kChildIdx]);

  // wait for the intermediate processs to be killed
  waitpid(middle_pid, NULL, 0);

  try {
    ddprof::Client client{ddprof::UnixSocket{sockfds[kParentIdx]}};
    auto info = client.get_profiler_info();
    g_state.profiler_pid = info.pid;
    if (info.allocation_profiling_rate > 0) {
      ddprof::AllocationTracker::allocation_tracking_init(
          info.allocation_profiling_rate, false, info.ring_buffer);
      g_state.allocation_profiling_started = true;
    }
  } catch (const ddprof::DDException &e) { return -1; }

  if (g_state.allocation_profiling_started) {
    // disable allocation profiling in child upon fork
    pthread_atfork(nullptr, nullptr, &allocation_profiling_stop);
  }
  g_state.started = true;
  set_profiler_library_active();
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

  if (g_state.allocation_profiling_started) {
    allocation_profiling_stop();
  }

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
