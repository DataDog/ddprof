// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_input.h"
#include "ddres.h"
#include "defer.hpp"
#include "ipc.hpp"

extern "C" {
#include "ddprof.h"
#include "ddprof_context.h"
#include "ddprof_context_lib.h"
#include "logger.h"
}

#include <cassert>
#include <errno.h>
#include <functional>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum class InputResult { kSuccess, kStop, kError };

// Parse input and initialize context
InputResult parse_input(int *argc, char ***argv, DDProfContext *ctx) {
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

  return InputResult::kSuccess;
}

// Daemonization function return some pid for daemon process, 0 for control
// (non-daemon) process, -1 for error.
// cleanup_function is a callable invoked in the context of the intermediate,
// short-lived process that will be killed by daemon process.
pid_t daemonize(std::function<void()> cleanup_function = {}) {
  pid_t temp_pid = fork(); // "middle" (temporary) PID

  if (!temp_pid) { // If I'm the temp PID enter branch
    temp_pid = getpid();
    if (pid_t child_pid = fork();
        child_pid) { // If I'm the temp PID again, enter branch
      if (cleanup_function) {
        cleanup_function();
      }
      // Block until our child exits or sends us a kill signal
      // NOTE, current process is NOT expected to unblock here; rather it
      // ends by SIGTERM.  Exiting here is an error condition.
      waitpid(child_pid, NULL, 0);
      return -1;
    } else {
      // If I'm the child PID, then leave and attach profiler
      return temp_pid;
    }
  } else {
    // If I'm the target PID, then now it's time to wait until my
    // child, the middle PID, returns.
    waitpid(temp_pid, NULL, 0);
    return 0;
  }
}

int start_profiler_internal(DDProfContext *ctx, bool &is_profiler) {
  defer { ddprof_context_free(ctx); };

  is_profiler = true;

  if (!ctx->params.enable) {
    LG_NFO("Profiling disabled");
    return 0;
  }

  pid_t temp_pid = 0;
  if (!ctx->params.pid) {
    // If no PID was specified earlier, we autodaemonize and target current pid
    ctx->params.pid = getpid();
    temp_pid = daemonize([ctx] { ddprof_context_free(ctx); });

    if (temp_pid == -1) {
      return -1;
    }

    // non-daemon process: return control to caller
    if (!temp_pid) {
      is_profiler = false;
      return 0;
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
        reply.ring_buffer.mem_size = perf_mmap_size(DEFAULT_BUFF_SIZE_SHIFT);
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
      LOG_ERROR_DETAILS(LG_ERR, e.get_DDRes()._what);
      return -1;
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

// This function only returns in daemon mode for control (non-daemon) process
void start_profiler(DDProfContext *ctx) {
  bool is_profiler = false;
  // ownership of context is passed to start_profiler_internal
  int res = start_profiler_internal(ctx, is_profiler);
  if (is_profiler) {
    exit(res);
  }
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
