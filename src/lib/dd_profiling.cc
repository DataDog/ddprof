// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dd_profiling.h"

#include "allocation_tracker.hpp"
#include "constants.hpp"
#include "daemonize.hpp"
#include "ddprof_cmdline.hpp"
#include "defer.hpp"
#include "ipc.hpp"
#include "lib_embedded_data.h"
#include "lib_logger.hpp"
#include "logger_setup.hpp"
#include "signal_helper.hpp"
#include "symbol_overrides.hpp"
#include "syscalls.hpp"

#include <cassert>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
int ddprof_start_profiling_internal();

struct ProfilerState {
  bool initialized = false;
  bool started = false;
  bool allocation_profiling_started = false;
  pid_t profiler_pid = 0;

  decltype(&::getenv) getenv = &::getenv;
  decltype(&::putenv) putenv = &::putenv;
  decltype(&::unsetenv) unsetenv = &::unsetenv;

  // Pointer on [01] char for "<k_profiler_active_env_variable>=[01]" env
  // variable. This allows to modify environment without calling putenv / setenv
  // in a thread safe way.
  char *profiler_active_indicator;

  static constexpr size_t profiler_active_len =
      std::char_traits<char>::length(k_profiler_active_env_variable) +
      std::char_traits<char>::length("=0");

  // profiler_active_str holds the string
  // "<k_profiler_active_env_variable>=[01]"
  char profiler_active_str[profiler_active_len + 1];
};

ProfilerState g_state;

void retrieve_original_env_functions() {
  /* Bash defines its own getenv/putenv functions
     Use dlsym with RTLD_NEXT to retrieve original functions from libc.
   */
  auto *getenv_ptr =
      reinterpret_cast<decltype(&::getenv)>(dlsym(RTLD_NEXT, "getenv"));
  if (getenv_ptr) {
    g_state.getenv = getenv_ptr;
  }
  auto *putenv_ptr =
      reinterpret_cast<decltype(&::putenv)>(dlsym(RTLD_NEXT, "putenv"));
  if (putenv_ptr) {
    g_state.putenv = putenv_ptr;
  }
  auto *unsetenv_ptr =
      reinterpret_cast<decltype(&::unsetenv)>(dlsym(RTLD_NEXT, "putenv"));
  if (unsetenv_ptr) {
    g_state.unsetenv = unsetenv_ptr;
  }
}

void init_profiler_library_active() {
  char *s = g_state.getenv(k_profiler_active_env_variable);

  if (!s || strlen(s) != 1 || (s[0] != '0' && s[0] != '1')) {
    (void)snprintf(g_state.profiler_active_str,
                   std::size(g_state.profiler_active_str), "%s=0",
                   k_profiler_active_env_variable);
    g_state.putenv(g_state.profiler_active_str);
    g_state.profiler_active_indicator = g_state.profiler_active_str +
        std::char_traits<char>::length(k_profiler_active_env_variable) + 1;
  } else {
    g_state.profiler_active_indicator = s;
  }
}

void init_state() {
  if (g_state.initialized) {
    return;
  }

  retrieve_original_env_functions();
  init_profiler_library_active();
  g_state.initialized = true;
}

// return true if this profiler is active for this process or one of its parent
bool is_profiler_library_active() {
  if (!g_state.initialized) {
    // should not happen
    return false;
  }
  return *g_state.profiler_active_indicator == '1';
}

void set_profiler_library_active() {
  if (!g_state.initialized) {
    // should not happen
    return;
  }
  *g_state.profiler_active_indicator = '1';
}

void set_profiler_library_inactive() {
  if (!g_state.initialized) {
    // should not happen
    return;
  }
  *g_state.profiler_active_indicator = '0';
}

void allocation_profiling_stop() {
  if (g_state.allocation_profiling_started) {
    ddprof::AllocationTracker::allocation_tracking_free();
    g_state.allocation_profiling_started = false;
  }
}

// Return socket created by ddprof when injecting lib if present
int get_ddprof_socket() {
  const char *socket_str = g_state.getenv(k_profiler_lib_socket_env_variable);
  if (socket_str) {
    std::string_view sv{socket_str};
    int sockfd = -1;
    if (auto [ptr, ec] = std::from_chars(sv.begin(), sv.end(), sockfd);
        ec == std::errc() && ptr == sv.end()) {
      return sockfd;
    }
  }
  return -1;
}

bool contains_lib(std::string_view ldpreload_str, std::string_view libname) {
  auto pos = ldpreload_str.find(libname);
  if (pos == std::string_view::npos) {
    return false;
  }

  if (pos > 0 && ldpreload_str[pos - 1] != '/') {
    return false;
  }

  auto remaining_str = ldpreload_str.substr(pos + libname.size());
  if (!remaining_str.empty()) {
    char const c = remaining_str.front();
    // space and colon are the allowed separators in LD_PRELOAD, dash is present
    // when hash is appended to libdd-profiling-embedded.so
    return c == ' ' || c == ':' || c == '-';
  }

  return true;
}

bool is_preloaded() {
  const char *ldpreload_str = g_state.getenv("LD_PRELOAD");
  if (!ldpreload_str) {
    return false;
  }
  return contains_lib(ldpreload_str, k_libdd_profiling_name) ||
      contains_lib(ldpreload_str, k_libdd_profiling_embedded_name) ||
      contains_lib(ldpreload_str, k_libdd_loader_name);
}

struct ProfilerAutoStart {
  ProfilerAutoStart() noexcept {
    init_state();

    // Note that library needs to be linked with `--no-as-needed` when using
    // autostart, otherwise linker will completely remove library from DT_NEEDED
    // and library will not be loaded.
    bool autostart = false;
    const char *autostart_env =
        g_state.getenv(k_profiler_auto_start_env_variable);
    if (autostart_env && arg_yesno(autostart_env, 1)) {
      autostart = true;
    } else if (is_preloaded()) {
      // if library is preloaded, autostart profiling since there is no way
      // otherwise to start profiling
      autostart = true;
    }

    init_profiler_library_active();

    // autostart if library is injected by ddprof
    if (autostart) {
      try {
        ddprof_start_profiling_internal();
      } catch (...) {}
    }
  }

  ~ProfilerAutoStart() {
    // profiler will stop when process exits
  }
};

ProfilerAutoStart g_autostart;

int exec_ddprof(pid_t target_pid, pid_t parent_pid, int sock_fd) {
  char ddprof_str[] = "ddprof";

  char pid_buf[32];
  snprintf(pid_buf, sizeof(pid_buf), "%d", target_pid);
  char sock_buf[32];
  snprintf(sock_buf, sizeof(sock_buf), "%d", sock_fd);

  char pid_opt_str[] = "-p";
  char sock_opt_str[] = "--socket";

  // cppcheck-suppress variableScope
  char *argv[] = {ddprof_str,   pid_opt_str, pid_buf,
                  sock_opt_str, sock_buf,    NULL};

  // unset LD_PRELOAD, otherwise if libdd_profiling.so was preloaded, it
  // would trigger a fork bomb
  g_state.unsetenv("LD_PRELOAD");

  kill(parent_pid, SIGTERM);

  if (const char *ddprof_exe =
          g_state.getenv(k_profiler_ddprof_exe_env_variable);
      ddprof_exe) {
    execve(ddprof_exe, argv, environ);
  } else {
    auto exe_data = profiler_exe_data();
    if (exe_data.size == 0) {
      return -1;
    }
    int fd = ddprof::memfd_create(ddprof_str, 1U /*MFD_CLOEXEC*/);
    if (fd == -1) {
      return -1;
    }
    defer { close(fd); };

    if (write(fd, exe_data.data, exe_data.size) !=
        static_cast<ssize_t>(exe_data.size)) {
      return -1;
    }
    fexecve(fd, argv, environ);
  }

  return -1;
}

void notify_fork() {
  ddprof::AllocationTracker::notify_fork();
  ddprof::reinstall_timer_after_fork();
}

int ddprof_start_profiling_internal() {
  // Refuse to start profiler if already started by this process or if active in
  // one of its ancestors
  if (g_state.started || is_profiler_library_active()) {
    return -1;
  }
  int sockfd = get_ddprof_socket();
  pid_t target_pid = getpid();

  // no socket -> library will spawn a profiler and create socket pair
  if (sockfd == -1) {
    // Create a socket pair to establish a communication channel between library
    // and profiler Communication occurs only during profiler setup and sockets
    // are closed once profiler is attached.
    // This channel is used to:
    //  * ensure that profiler is attached before returning
    //    from ddprof_start_profiling
    //  * retrieve PID of profiler on library side
    //  * retrieve ring buffer information for allocation profiling on library
    //    side. This requires passing file descriptors between processes,
    //    that's why unix socket are used here.
    // Overview of the communication process:
    //  * library create socket pair and fork + exec into ddprof executable
    //  * library sends a message requesting profiler PID and waits for a reply
    //  * ddprof starts up, attaches itself to the target process, then waits
    //    for a request and replies
    //  * both library and profiler close their socket and continue
    int sockfds[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockfds) == -1) {
      return -1;
    }

    ddprof::UniqueFd parent_socket{sockfds[0]};
    ddprof::UniqueFd child_socket{sockfds[1]};

    auto daemonize_res = ddprof::daemonize();
    if (daemonize_res.state == ddprof::DaemonizeResult::Error) {
      return -1;
    }

    if (daemonize_res.state == ddprof::DaemonizeResult::IntermediateProcess) {
      _exit(0);
    }

    if (daemonize_res.state == ddprof::DaemonizeResult::DaemonProcess) {
      // executed by daemonized process

      // close parent socket end
      parent_socket.reset();
      int child_socket_fd = child_socket.release();

      exec_ddprof(target_pid, daemonize_res.temp_pid, child_socket_fd);
      exit(1);
    }

    sockfd = parent_socket.release();
  }

  try {
    ddprof::Client client{ddprof::UnixSocket{sockfd}};
    auto info = client.get_profiler_info();
    g_state.profiler_pid = info.pid;
    if (info.allocation_profiling_rate != 0) {
      uint32_t flags{0};
      // Negative profiling rate is interpreted as deterministic sampling rate
      if (info.allocation_profiling_rate < 0) {
        flags |= ddprof::AllocationTracker::kDeterministicSampling;
        info.allocation_profiling_rate = -info.allocation_profiling_rate;
      }

      if (info.allocation_flags & (1 << ddprof::ReplyMessage::kLiveCallgraph)) {
        // tracking deallocations to allow a live view
        flags |= ddprof::AllocationTracker::kTrackDeallocations;
      }

      if (IsDDResOK(ddprof::AllocationTracker::allocation_tracking_init(
              info.allocation_profiling_rate, flags, info.stack_sample_size,
              info.ring_buffer))) {
        // \fixme{nsavoire} pthread_create should probably be overridden
        // at load time since we need to capture stack end addresses of all
        // threads in case allocation profiling is started later on
        ddprof::setup_overrides(
            std::chrono::milliseconds{info.initial_loaded_libs_check_delay_ms},
            std::chrono::milliseconds{info.loaded_libs_check_interval_ms});
        // \fixme{nsavoire} what should we do when allocation tracker init
        // fails ?
        g_state.allocation_profiling_started = true;
      } else {
        ddprof::log_once("Error: %s",
                         "Failure to start allocation profiling\n");
      }
    }
  } catch (const ddprof::DDException &e) { return -1; }

  if (g_state.allocation_profiling_started) {
    int res = pthread_atfork(nullptr, nullptr, notify_fork);
    if (res) {
      ddprof::log_once("Error:%s", "Unable to setup notify fork");
      assert(0);
    }
  }
  g_state.started = true;
  set_profiler_library_active();
  return 0;
}

} // namespace

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
    if (!process_is_alive(g_state.profiler_pid)) {
      return;
    }
  }

  // timeout reached and profiler process is still not dead
  // Do a forceful kill
  kill(g_state.profiler_pid, SIGKILL);
}
