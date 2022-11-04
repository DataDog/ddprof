// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "constants.hpp"
#include "daemonize.hpp"
#include "ddprof.hpp"
#include "ddprof_context.hpp"
#include "ddprof_context_lib.hpp"
#include "ddprof_cpumask.hpp"
#include "ddprof_input.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "ipc.hpp"
#include "logger.hpp"
#include "tempfile.hpp"
#include "timer.hpp"
#include "user_override.hpp"

#include <array>
#include <cassert>
#include <charconv>
#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

enum class InputResult { kSuccess, kStop, kError };

// address of embedded libddprofiling shared library
extern const char
    _binary_libdd_profiling_embedded_so_start[]; // NOLINT cert-dcl51-cpp
extern const char
    _binary_libdd_profiling_embedded_so_end[]; // NOLINT cert-dcl51-cpp

#ifdef DDPROF_USE_LOADER
// address of embedded libddloader shared library
extern const char _binary_libdd_loader_so_start[]; // NOLINT cert-dcl51-cpp
extern const char _binary_libdd_loader_so_end[];   // NOLINT cert-dcl51-cpp
#endif

static void maybe_slowdown_startup() {
  // Simulate startup slowdown if requested
  if (const char *s = getenv(k_startup_wait_ms_env_variable); s != nullptr) {
    std::string_view sv{s};
    int wait_ms = 0;
    auto [ptr, ec] = std::from_chars(sv.begin(), sv.end(), wait_ms);
    if (ec == std::errc() && ptr == sv.end() && wait_ms > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
    }
  }
}

static std::string find_lib(std::string_view lib_name) {
  auto exe_path = fs::read_symlink("/proc/self/exe");
  auto lib_path = exe_path.parent_path() / lib_name;
  // first, check if libdd_profiling.so exists in same directory as exe or in
  // <exe_path>/../lib/
  if (fs::exists(lib_path)) {
    return lib_path;
  }
  lib_path = exe_path.parent_path().parent_path() / "lib" / lib_name;
  if (fs::exists(lib_path)) {
    return lib_path;
  }
  return {};
}

class TempFileHolder {
public:
  TempFileHolder() = default;
  TempFileHolder(std::string path, bool is_temporary)
      : _path(std::move(path)), _is_temporary(is_temporary) {}

  ~TempFileHolder() {
    if (_is_temporary && !_path.empty()) {
      unlink(_path.c_str());
    }
  }

  TempFileHolder(const TempFileHolder &) = delete;
  TempFileHolder &operator=(const TempFileHolder &) = delete;

  TempFileHolder(TempFileHolder &&other) : TempFileHolder() {
    *this = std::move(other);
  }

  TempFileHolder &operator=(TempFileHolder &&other) {
    using std::swap;
    swap(_path, other._path);
    swap(_is_temporary, other._is_temporary);
    return *this;
  }

  const std::string &path() const { return _path; }

  bool is_temporary() const { return _is_temporary; }

  std::string release() {
    std::string s = std::move(_path);
    _path.clear();
    _is_temporary = false;
    return s;
  }

private:
  std::string _path;
  bool _is_temporary = false;
};

static DDRes get_library_path(TempFileHolder &libdd_profiling_path,
                              TempFileHolder &libdd_loader_path) {
  std::string profiling_path;
  std::string loader_path;

  if (!getenv(k_profiler_use_embedded_libdd_profiling_env_variable)) {
    profiling_path = find_lib(k_libdd_profiling_embedded_name);
    loader_path = find_lib(k_libdd_loader_name);
  }

  if (profiling_path.empty()) {
    DDRES_CHECK_FWD(get_or_create_temp_file(
        k_libdd_profiling_embedded_name,
        ddprof::as_bytes(ddprof::span{_binary_libdd_profiling_embedded_so_start,
                                      _binary_libdd_profiling_embedded_so_end}),
        0644, profiling_path));
    libdd_profiling_path = TempFileHolder{profiling_path, false};
  } else {
    libdd_profiling_path = TempFileHolder{profiling_path, false};
  }

#ifdef DDPROF_USE_LOADER
  if (loader_path.empty()) {
    DDRES_CHECK_FWD(get_or_create_temp_file(
        k_libdd_loader_name,
        ddprof::as_bytes(ddprof::span{_binary_libdd_loader_so_start,
                                      _binary_libdd_loader_so_end}),
        0644, loader_path));
    libdd_loader_path = TempFileHolder{loader_path, false};
  } else {
    libdd_loader_path = TempFileHolder{loader_path, false};
  }
#else
  (void)libdd_loader_path;
#endif

  return {};
}

// Parse input and initialize context
static InputResult parse_input(int *argc, char ***argv, DDProfContext *ctx) {
  //---- Inititiate structs
  *ctx = {};
  // Set temporary logger for argument parsing
  LOG_open(LOG_STDERR, NULL);
  LOG_setlevel(LL_WARNING);

  DDProfInput input = {};
  defer { ddprof_input_free(&input); };
  bool continue_exec;
  DDRes res = ddprof_input_parse(*argc, *argv, &input, &continue_exec);
  if (IsDDResNotOK(res) || !continue_exec) {
    return IsDDResOK(res) ? InputResult::kStop : InputResult::kError;
  }

  // logger can be closed (as it is opened in ddprof_context_set)
  LOG_close();

  // cmdline args have been processed.  Set the ctx
  if (IsDDResNotOK(ddprof_context_set(&input, ctx))) {
    LG_ERR("Error setting up profiling context, exiting");
    ddprof_context_free(ctx);
    return InputResult::kError;
  }
  // Adjust input parameters for execvp() (we do this even if unnecessary)
  *argv += input.nb_parsed_params;
  *argc -= input.nb_parsed_params;

  // Only throw an error if we needed the user to pass an arg
  if (ctx->params.pid) {
    if (*argc > 0) {
      LG_ERR("Unexpected trailing arguments in PID mode");
      return InputResult::kError;
    }
    if (ctx->params.pid == -1)
      LG_NFO("Instrumenting whole system");
    else
      LG_NFO("Instrumenting PID %d", ctx->params.pid);
  } else if (*argc <= 0) {
    LG_ERR("No target specified, exiting");
    return InputResult::kError;
  }

  if (ddprof_context_allocation_profiling_watcher_idx(ctx) != -1 &&
      ctx->params.pid && ctx->params.sockfd == -1) {
    LG_ERR("Memory allocation profiling is not supported in PID / global mode");
    return InputResult::kError;
  }

  return InputResult::kSuccess;
}

static int start_profiler_internal(DDProfContext *ctx, bool &is_profiler) {
  auto defer_context_free = make_defer([ctx] { ddprof_context_free(ctx); });

  is_profiler = false;

  if (!ctx->params.enable) {
    LG_NFO("Profiling disabled");
    return 0;
  }

  const bool in_wrapper_mode = ctx->params.pid == 0;
  TempFileHolder dd_profiling_lib_holder, dd_loader_lib_holder;

  pid_t temp_pid = 0;
  if (in_wrapper_mode) {
    // If no PID was specified earlier, we autodaemonize and target current pid

    // Determine if library should be injected into target process
    // (ie. only if allocation profiling is active)
    bool allocation_profiling_started_from_wrapper =
        ddprof_context_allocation_profiling_watcher_idx(ctx) != -1;

    enum { kParentIdx, kChildIdx };
    int sockfds[2] = {-1, -1};

    auto defer_child_socket_close = make_defer([&sockfds]() {
      if (sockfds[kChildIdx] != -1)
        close(sockfds[kChildIdx]);
    });
    auto defer_parent_socket_close = make_defer([&sockfds]() {
      if (sockfds[kParentIdx] != -1)
        close(sockfds[kParentIdx]);
    });

    if (allocation_profiling_started_from_wrapper) {
      if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sockfds) == -1) {
        return -1;
      }
      if (!IsDDResOK(get_library_path(dd_profiling_lib_holder,
                                      dd_loader_lib_holder))) {
        return -1;
      }
      LG_DBG("ctx->params.dd_profiling_fd = %d - sockfds %d, %d",
             ctx->params.dd_profiling_fd, sockfds[kChildIdx],
             sockfds[kParentIdx]);
      ctx->params.sockfd = sockfds[kChildIdx];
      ctx->params.wait_on_socket = true;
    }

    ctx->params.pid = getpid();
    auto daemonize_res = ddprof::daemonize([ctx] { ddprof_context_free(ctx); });

    if (daemonize_res.temp_pid == -1) {
      return -1;
    }

    temp_pid = daemonize_res.temp_pid;
    if (!temp_pid) {
      // non-daemon process: return control to caller
      defer_child_socket_close.reset();
      defer_context_free.release();

      std::string dd_loader_lib_path = dd_loader_lib_holder.release();
      std::string dd_profiling_lib_path = dd_profiling_lib_holder.release();

      // Allocation profiling activated, inject dd_profiling library with
      // LD_PRELOAD
      if (allocation_profiling_started_from_wrapper) {
        std::string preload_str = dd_loader_lib_path.empty()
            ? dd_profiling_lib_path
            : dd_loader_lib_path;
        if (const char *s = getenv("LD_PRELOAD"); s) {
          preload_str.append(":");
          preload_str.append(s);
        }
        LG_DBG("Setting LD_PRELOAD=%s", preload_str.c_str());
        setenv("LD_PRELOAD", preload_str.c_str(), 1);
        if (!dd_loader_lib_path.empty()) {
          setenv("DD_PROFILING_NATIVE_LIBRARY", dd_profiling_lib_path.c_str(),
                 1);
        }
        auto sock_str = std::to_string(sockfds[kParentIdx]);
        setenv(k_profiler_lib_socket_env_variable, sock_str.c_str(), 1);
      }

      defer_parent_socket_close.release();
      return 0;
    }
    defer_child_socket_close.release();
    defer_parent_socket_close.reset();
  }

  // Now, we are the profiler process
  is_profiler = true;

  ddprof::init_tsc();

  if (CPU_COUNT(&ctx->params.cpu_affinity) > 0) {
    LG_DBG("Setting affinity to 0x%s",
           ddprof::cpu_mask_to_string(ctx->params.cpu_affinity).c_str());
    if (sched_setaffinity(0, sizeof(cpu_set_t), &ctx->params.cpu_affinity) !=
        0) {
      LG_ERR("Failed to set profiler CPU affinity to 0x%s: %s",
             ddprof::cpu_mask_to_string(ctx->params.cpu_affinity).c_str(),
             strerror(errno));
      return -1;
    }
  }

  // Attach the profiler
  if (IsDDResNotOK(ddprof_setup(ctx))) {
    LG_ERR("Failed to initialize profiling");
    return -1;
  }

  defer { ddprof_teardown(ctx); };

  // If we have a temp PID, then it's waiting for us to send it a signal
  // after we finish instrumenting.  This will end that process, which in
  // turn will unblock the target from calling exec.
  if (temp_pid) {
    kill(temp_pid, SIGTERM);
  }

  if (ctx->params.sockfd != -1 && ctx->params.wait_on_socket) {
    ddprof::ReplyMessage reply;
    reply.request = ddprof::RequestMessage::kProfilerInfo;
    reply.pid = getpid();

    int alloc_watcher_idx =
        ddprof_context_allocation_profiling_watcher_idx(ctx);
    if (alloc_watcher_idx != -1) {
      ddprof::span pevents{ctx->worker_ctx.pevent_hdr.pes,
                           ctx->worker_ctx.pevent_hdr.size};
      auto event_it =
          std::find_if(pevents.begin(), pevents.end(),
                       [alloc_watcher_idx](const auto &pevent) {
                         return pevent.watcher_pos == alloc_watcher_idx;
                       });
      if (event_it != pevents.end()) {
        reply.ring_buffer.event_fd = event_it->fd;
        reply.ring_buffer.ring_fd = event_it->mapfd;
        reply.ring_buffer.mem_size = event_it->ring_buffer_size;
        reply.ring_buffer.ring_buffer_type =
            static_cast<int>(event_it->ring_buffer_type);
        reply.allocation_profiling_rate =
            ctx->watchers[alloc_watcher_idx].sample_period;
      }
    }

    try {
      // Takes ownership of the open socket, socket will be closed when
      // exiting this block
      ddprof::Server server{ddprof::UnixSocket{ctx->params.sockfd}};
      ctx->params.sockfd = -1;

      server.waitForRequest(
          [&reply](const ddprof::RequestMessage &) { return reply; });
    } catch (const ddprof::DDException &e) {
      if (in_wrapper_mode) {
        // Failture in wrapper mode is not fatal:
        // LD_PRELOAD may fail because target exe is statically linked
        // (eg. go binaries)
        LG_WRN("Unable to connect to profiler library (target executable might "
               "be statically linked and library cannot be preloaded). "
               "Allocation profiling will be disabled.");
      } else {
        LOG_ERROR_DETAILS(LG_ERR, e.get_DDRes()._what);
        return -1;
      }
    }
  }

  LG_NTC("Starting profiler");

  maybe_slowdown_startup();

  // Now enter profiling
  DDRes res = ddprof_start_profiler(ctx);
  if (IsDDResNotOK(res)) {
    // Some kind of error; tell the user about what happened in one line
    LG_ERR("Profiling terminated (%s)", ddres_error_message(res._what));
    return -1;
  } else {
    // Normal error -- don't overcommunicate
    LG_NTC("Profiling terminated");
  }

  return 0;
}

// This function only returns in wrapper mode for control (non-ddprof) process
static void start_profiler(DDProfContext *ctx) {
  bool is_profiler = false;

  // ownership of context is passed to start_profiler_internal
  int res = start_profiler_internal(ctx, is_profiler);
  if (is_profiler) {
    exit(res);
  }
  // In wrapper mode (ie. ctx->params.pid == 0), whatever happened to ddprof,
  // continue and start user process
  return;
}

/**************************** Program Entry Point *****************************/
int main(int argc, char *argv[]) {
  DDProfContext ctx;

  // parse inputs and populate context
  switch (parse_input(&argc, &argv, &ctx)) {
  case InputResult::kStop:
    return 0;
  case InputResult::kSuccess:
    break;
  case InputResult::kError:
    [[fallthrough]];
  default:
    return -1;
  }

  {
    defer { ddprof_context_free(&ctx); };
    /****************************************************************************\
    |                             Run the Profiler |
    \****************************************************************************/
    // Ownership of context is passed to start_profiler
    // This function does not return in the context of profiler process
    // It only returns in the context of target process (ie. in non-PID mode)
    start_profiler(&ctx);

    if (ctx.params.switch_user) {
      if (!IsDDResOK(become_user(ctx.params.switch_user))) {
        LG_ERR("Failed to switch to user %s", ctx.params.switch_user);
        return -1;
      }
    }
  }

  // Execute manages its own return path
  if (-1 == execvp(*argv, (char *const *)argv)) {
    switch (errno) {
    case ENOENT:
      LG_ERR("%s: file not found", argv[0]);
      break;
    case ENOEXEC:
    case EACCES:
      LG_ERR("%s: permission denied", argv[0]);
      break;
    default:
      LG_ERR("%s: failed to execute (%s)", argv[0], strerror(errno));
      break;
    }
  }

  return -1;
}
