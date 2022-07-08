// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "constants.hpp"
#include "daemonize.hpp"
#include "ddprof.hpp"
#include "ddprof_context.hpp"
#include "ddprof_context_lib.hpp"
#include "ddprof_input.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "ipc.hpp"
#include "logger.hpp"
#include "timer.hpp"

#include <array>
#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

enum class InputResult { kSuccess, kStop, kError };

// address of embedded libddprofiling shared library
extern const char
    _binary_libdd_profiling_embedded_so_start[]; // NOLINT cert-dcl51-cpp
extern const char
    _binary_libdd_profiling_embedded_so_end[]; // NOLINT cert-dcl51-cpp

static constexpr const char k_pid_place_holder[] = "{pid}";

static DDRes get_library_path(std::string &path, int &fd) {
  fd = -1;
  auto exe_path = fs::read_symlink("/proc/self/exe");
  auto lib_path = exe_path.parent_path() / k_libdd_profiling_name;
  // first, check if libdd_profiling.so exists in same directory as exe or in
  // <exe_path>/../lib/
  if (fs::exists(lib_path)) {
    path = lib_path;
    return {};
  }
  lib_path =
      exe_path.parent_path().parent_path() / "lib" / k_libdd_profiling_name;
  if (fs::exists(lib_path)) {
    path = lib_path;
    return {};
  }

  // Did not find libdd_profiling.so, use the one embedded in ddprof exe
  path = std::string{fs::temp_directory_path() / k_libdd_profiling_name} +
      ".XXXXXX";

  // Create temporary file
  fd = mkostemp(path.data(), O_CLOEXEC);
  DDRES_CHECK_ERRNO(fd, DD_WHAT_TEMP_FILE, "Failed to create temporary file");

  // Write embedded lib into temp file
  ssize_t lib_sz = _binary_libdd_profiling_embedded_so_end -
      _binary_libdd_profiling_embedded_so_start;
  if (write(fd, _binary_libdd_profiling_embedded_so_start, lib_sz) != lib_sz) {
    DDRES_CHECK_ERRNO(fd, DD_WHAT_TEMP_FILE, "Failed to write temporary file");
  }

  // Unlink temp file, that way file will disappear from filesystem once last
  // file descriptor pointing to it is closed
  unlink(path.data());
  char buffer[1024];

  // Use symlink from /proc/<ddprof_pid>/fd/<fd> to refer to file.
  // ddprof pid is not known yet so use a place holder that will be replaced
  // later on
  sprintf(buffer, "/proc/%s/fd/%d", k_pid_place_holder, fd);
  path = buffer;
  return {};
}

// Replace pid place holder in path if present by pid argument
static void fixup_library_path(std::string &path, pid_t pid) {
  if (size_t pos = path.find(k_pid_place_holder); pos != std::string::npos) {
    path.replace(pos, std::size(k_pid_place_holder) - 1, std::to_string(pid));
  }
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
  defer { ddprof_context_free(ctx); };

  is_profiler = false;

  if (!ctx->params.enable) {
    LG_NFO("Profiling disabled");
    return 0;
  }

  const bool in_wrapper_mode = ctx->params.pid == 0;

  pid_t temp_pid = 0;
  if (in_wrapper_mode) {
    // If no PID was specified earlier, we autodaemonize and target current pid

    // Determine if library should be injected into target process
    // (ie. only if allocation profiling is active)
    bool allocation_profiling_started_from_wrapper =
        ddprof_context_allocation_profiling_watcher_idx(ctx) != -1;

    std::string dd_profiling_lib_path;

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
      if (!IsDDResOK(get_library_path(dd_profiling_lib_path, ctx->params.fd_dd_profiling))) {
        return -1;
      }
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

      // Allocation profiling activated, inject dd_profiling library with
      // LD_PRELOAD
      if (allocation_profiling_started_from_wrapper) {
        // Determine final lib profiling path now that ddprof pid is known
        fixup_library_path(dd_profiling_lib_path, daemonize_res.daemon_pid);
        std::string preload_str = dd_profiling_lib_path;
        if (const char *s = getenv("LD_PRELOAD"); s) {
          preload_str.append(";");
          preload_str.append(s);
        }
        LG_DBG("Setting LD_PRELOAD=%s", preload_str.c_str());
        setenv("LD_PRELOAD", preload_str.c_str(), 1);
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

  /****************************************************************************\
  |                             Run the Profiler                               |
  \****************************************************************************/
  // Ownership of context is passed to start_profiler
  // This function does not return in the context of profiler process
  // It only returns in the context of target process (ie. in non-PID mode)
  start_profiler(&ctx);

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
      LG_WRN("%s: failed to execute (%s)", argv[0], strerror(errno));
      break;
    }
  }

  return -1;
}
