// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "constants.hpp"
#include "daemonize.hpp"
#include "ddprof.hpp"
#include "ddprof_cli.hpp"
#include "ddprof_context.hpp"
#include "ddprof_context_lib.hpp"
#include "ddprof_cpumask.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "ipc.hpp"
#include "logger.hpp"
#include "signal_helper.hpp"
#include "tempfile.hpp"
#include "timer.hpp"
#include "unique_fd.hpp"
#include "user_override.hpp"

#include <array>
#include <charconv>
#include <errno.h>
#include <filesystem>
#include <functional>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace ddprof;
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

  // If not forbidden by env variable, try first to locate profiling and loader
  // libs in ddprof exe directory. This makes debugging easier.
  if (!getenv(k_profiler_use_embedded_libdd_profiling_env_variable)) {
    profiling_path = find_lib(k_libdd_profiling_embedded_name);
    loader_path = find_lib(k_libdd_loader_name);
  }

  if (profiling_path.empty()) {
    DDRES_CHECK_FWD(ddprof::get_or_create_temp_file(
        k_libdd_profiling_embedded_name,
        as_bytes(std::span{_binary_libdd_profiling_embedded_so_start,
                           _binary_libdd_profiling_embedded_so_end}),
        0644, profiling_path));
    libdd_profiling_path = TempFileHolder{profiling_path, false};
  } else {
    libdd_profiling_path = TempFileHolder{profiling_path, false};
  }

#ifdef DDPROF_USE_LOADER
  if (loader_path.empty()) {
    DDRES_CHECK_FWD(ddprof::get_or_create_temp_file(
        k_libdd_loader_name,
        as_bytes(std::span{_binary_libdd_loader_so_start,
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

namespace {
DDRes check_incompatible_options(const DDProfContext &ctx) {
  if (context_allocation_profiling_watcher_idx(ctx) != -1 && ctx.params.pid &&
      !ctx.params.sockfd) {
    DDRES_RETURN_ERROR_LOG(
        DD_WHAT_INPUT_PROCESS,
        "Memory allocation profiling is not supported in PID / global mode");
  }
  return {};
}
} // namespace
// Parse input and initialize context
static DDRes parse_input(const ddprof::DDProfCLI &ddprof_cli,
                         DDProfContext &ctx) {

  // cmdline args have been processed.  Set the ctx
  DDRES_CHECK_FWD(ddprof::context_set(ddprof_cli, ctx));

  DDRES_CHECK_FWD(check_incompatible_options(ctx));

  return {};
}

static int start_profiler_internal(DDProfContext *ctx, bool &is_profiler) {
  is_profiler = false;

  if (!ctx->params.enable) {
    LG_WRN("Profiling disabled");
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
        ddprof::context_allocation_profiling_watcher_idx(*ctx) != -1;

    ddprof::UniqueFd child_socket;
    ddprof::UniqueFd parent_socket;

    if (allocation_profiling_started_from_wrapper) {
      int sockfds[2] = {-1, -1};
      if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sockfds) == -1) {
        return -1;
      }
      parent_socket.reset(sockfds[0]);
      child_socket.reset(sockfds[1]);

      if (!IsDDResOK(get_library_path(dd_profiling_lib_holder,
                                      dd_loader_lib_holder))) {
        return -1;
      }
      LG_DBG("ctx->params.dd_profiling_fd = %d - sockfds %d, %d",
             ctx->params.dd_profiling_fd, child_socket.get().get(),
             parent_socket.get().get());
    }

    ctx->params.pid = getpid();
    auto daemonize_res = ddprof::daemonize();

    if (daemonize_res.state == ddprof::DaemonizeResult::Error) {
      return -1;
    }

    if (daemonize_res.state == ddprof::DaemonizeResult::IntermediateProcess) {
      // temp intermediate process,: return and exit
      is_profiler = true;
      return 0;
    }

    if (daemonize_res.state == ddprof::DaemonizeResult::InitialProcess) {
      // non-daemon process: return control to caller
      child_socket.reset();

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
        auto sock_str = std::to_string(parent_socket.get());
        setenv(k_profiler_lib_socket_env_variable, sock_str.c_str(), 1);
      }

      parent_socket.release();
      return 0;
    }

    // profiler process
    ctx->params.sockfd = std::move(child_socket);
    temp_pid = daemonize_res.temp_pid;
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
  if (IsDDResNotOK(ddprof_setup(*ctx))) {
    LG_ERR("Failed to initialize profiling");
    return -1;
  }

  defer { ddprof_teardown(*ctx); };

  // If we have a temp PID, then it's waiting for us to send it a signal
  // after we finish instrumenting.  This will end that process, which in
  // turn will unblock the target from calling exec.
  if (temp_pid) {
    kill(temp_pid, SIGTERM);
  }

  if (ctx->params.sockfd) {
    ReplyMessage reply;
    reply.request = RequestMessage::kProfilerInfo;
    reply.pid = getpid();

    int alloc_watcher_idx =
        ddprof::context_allocation_profiling_watcher_idx(*ctx);
    if (alloc_watcher_idx != -1) {
      std::span pevents{ctx->worker_ctx.pevent_hdr.pes,
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
        reply.stack_sample_size =
            ctx->watchers[alloc_watcher_idx].options.stack_sample_size;
        reply.initial_loaded_libs_check_delay_ms =
            ctx->params.initial_loaded_libs_check_delay.count();
        reply.loaded_libs_check_interval_ms =
            ctx->params.loaded_libs_check_interval.count();

        if (ctx->watchers[alloc_watcher_idx].output_mode ==
            EventConfMode::kLiveCallgraph) {
          reply.allocation_flags |= (1 << ddprof::ReplyMessage::kLiveCallgraph);
        }
      }
    }

    try {
      // Takes ownership of the open socket, socket will be closed when
      // exiting this block
      Server server{UnixSocket{ctx->params.sockfd.release()}};

      server.waitForRequest(
          [&reply](const ddprof::RequestMessage &) { return reply; });
    } catch (const ddprof::DDException &e) {
      if (in_wrapper_mode) {
        if (!process_is_alive(ctx->params.pid)) {
          // Tell the user that process died
          // Most of the time this is an invalid command line
          LG_WRN(
              "Target process(%d) is not alive. Allocation profiling stopped.",
              ctx->params.pid);
          // We are not returning
          // We could still have a short lived process that has forked
          // CPU profiling will stop on a later failure if process died.
        } else {
          // Failure in wrapper mode is not fatal:
          // LD_PRELOAD may fail because target exe is statically linked
          // (eg. go binaries)
          LG_WRN(
              "Unable to connect to profiler library (target executable might "
              "be statically linked and library cannot be preloaded). "
              "Allocation profiling will be disabled.");
        }
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
    delete ctx;
    exit(res);
  }
  // In wrapper mode (ie. ctx->params.pid == 0), whatever happened to ddprof,
  // continue and start user process
  return;
}

/**************************** Program Entry Point *****************************/
int main(int argc, char *argv[]) {
  ddprof::CommandLineWrapper cmd_line({});
  {
    // Use a dynamic allocation to allow clean up
    // in other exit flows
    DDProfContext *ctx = new DDProfContext();
    defer { delete ctx; };
    {
      ddprof::DDProfCLI cli;
      int res = cli.parse(argc, const_cast<const char **>(argv));
      if (!cli.continue_exec) {
        return res;
      }

      // parse inputs and populate context
      if (IsDDResNotOK(parse_input(cli, *ctx))) {
        return -1;
      }
      cmd_line = cli.get_user_command_line(); // Get the command line before cli
                                              // goes out of scope.

    } // cli is destroyed here (prevents forks from having an instance of CLI

    /**************************************************************************\
    |                             Run the Profiler |
    \**************************************************************************/
    // Ownership of context is passed to start_profiler
    // This function does not return in the context of profiler process
    // It only returns in the context of target process (ie. in non-PID mode)
    start_profiler(ctx);

    if (!ctx->params.switch_user.empty()) {
      if (!IsDDResOK(become_user(ctx->params.switch_user.c_str()))) {
        LG_ERR("Failed to switch to user %s", ctx->params.switch_user.c_str());
        return -1;
      }
    }

    if (cmd_line.get().empty()) {
      fprintf(stderr, "Empty command line. Exiting");
      return -1;
    }
  } // ctx end of life

  if (-1 == execvp(cmd_line.get()[0], cmd_line.get().data())) {
    // Logger is not configured in the context of the parent process:
    // We use stderr as a standard logging mechanism
    switch (errno) {
    case ENOENT:
      fprintf(stderr, "%s: executable not found", argv[0]);
      break;
    case ENOEXEC:
    case EACCES:
      fprintf(stderr, "%s: permission denied", argv[0]);
      break;
    default:
      fprintf(stderr, "%s: failed to execute (%s)", argv[0], strerror(errno));
      break;
    }
  }

  return -1;
}
