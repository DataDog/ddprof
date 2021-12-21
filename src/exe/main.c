// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof.h"

#include "ddprof_context.h"
#include "ddprof_context_lib.h"
#include "ddprof_input.h"
#include "logger.h"
#include "unwind.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/**************************** Program Entry Point *****************************/
int main(int argc, char *argv[]) {
  //---- Inititiate structs
  int ret = -1;
  DDProfInput input;
  DDProfContext ctx = {0};
  // Set temporary logger for argument parsing
  LOG_open(LOG_STDERR, NULL);
  LOG_setlevel(LL_WARNING);

  {
    bool continue_exec;
    DDRes res = ddprof_input_parse(argc, (char **)argv, &input, &continue_exec);
    if (IsDDResNotOK(res) || !continue_exec) {
      if (IsDDResOK(res)) {
        // Input parsing calls out to print commands, like for the version or
        // the help.  If we performed one of those, we are done, but it is not
        // an error.
        ret = 0;
      }
      ddprof_input_free(&input);
      goto CLEANUP;
    }
  }
  // logger can be closed (as it is opened in ddprof_context_set)
  LOG_close();

  // cmdline args have been processed.  Set the ctx
  if (IsDDResNotOK(ddprof_context_set(&input, &ctx))) {
    LG_ERR("Error setting up profiling context, exiting");
    goto CLEANUP;
  }
  // Adjust input parameters for execvp() (we do this even if unnecessary)

  argv += input.nb_parsed_params;
  argc -= input.nb_parsed_params;

  // input no longer needed, ctx and args are set
  ddprof_input_free(&input);

  // Only throw an error if we needed the user to pass an arg
  if (ctx.params.pid) {
    if (ctx.params.pid == -1)
      LG_NFO("Instrumenting whole system");
    else
      LG_NFO("Instrumenting PID %d", ctx.params.pid);
  } else if (argc <= 0) {
    LG_ERR("No target specified, exiting");
    goto CLEANUP;
  }

  /****************************************************************************\
  |                             Run the Profiler                               |
  \****************************************************************************/
  // If the profiler was disabled, just skip ahead to EXECUTE
  if (!ctx.params.enable) {
    LG_NFO("Profiling disabled");
  } else {
    pid_t temp_pid = 0, child_pid; // child pid is for the profiler
    // If no PID was specified earlier, we autodaemonize and launch command
    if (!ctx.params.pid) {
      ctx.params.pid = getpid(); // "parent" (target) PID
      temp_pid = fork();         // "middle" (temporary) PID

      if (!temp_pid) { // If I'm the temp PID enter branch
        temp_pid = getpid();
        if ((child_pid = fork())) { // If I'm the temp PID again, enter branch
          ddprof_context_free(&ctx);
          // Block until our child exits or sends us a kill signal
          // NOTE, current process is NOT expected to unblock here; rather it
          // ends by SIGTERM.  Exiting here is an error condition.
          waitpid(child_pid, NULL, 0);
          return ret;
        } else {
        }      // If I'm the child PID, then leave and attach profiler
      } else { // If I'm the target PID, then now it's time to wait until my
               // child, the middle PID, returns.  That's the cue to exec.
        waitpid(temp_pid, NULL, 0);
        goto EXECUTE;
      }
    }

    // Attach the profiler
    if (IsDDResNotOK(ddprof_setup(&ctx, ctx.params.pid))) {
      LG_ERR("Failed to initialize profiling");
      goto CLEANUP;
    }

    // If we have a temp PID, then it's waiting for us to send it a signal
    // after we finish instrumenting.  This will end that process, which in turn
    // will unblock the target from calling exec.
    if (temp_pid)
      kill(temp_pid, SIGTERM);

    // Now enter profiling
    DDRes res = ddprof_start_profiler(&ctx);
    if (IsDDResNotOK(res)) {
      // Some kind of error; tell the user about what happened in one line
      LG_ERR("Profiling terminated (%s)", ddres_error_message(res._what));
    } else {
      // Normal error -- don't overcommunicate
      LG_WRN("Profiling terminated");
    }
    goto CLEANUP;
  }

// Execute manages its own return path, since we
EXECUTE:
  ddprof_context_free(&ctx);
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

CLEANUP:
  ddprof_context_free(&ctx); // no-op if ctx already freed
  return ret;
}
