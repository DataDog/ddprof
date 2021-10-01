#include "ddprof.h"

#include "ddprof_context.h"
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
  int ret = 0;
  DDProfInput input;
  DDProfContext ctx;
  // Set temporary logger for argument parsing
  LOG_open(LOG_STDERR, NULL);
  LOG_setlevel(LL_WARNING);

  {
    bool continue_exec;
    DDRes res = ddprof_input_parse(argc, (char **)argv, &input, &continue_exec);
    if (IsDDResNotOK(res) || !continue_exec) {
      LG_WRN("Unable to parse parameters");
      goto CLEANUP_INPUT;
    }
  }
  // logger can be closed (as it is opened in ddprof_ctx_set)
  LOG_close();

  // cmdline args have been processed.  Set the ctx
  if (IsDDResNotOK(ddprof_ctx_set(&input, &ctx))) {
    LG_ERR("Error setting up profiling context, exiting");
    goto CLEANUP_ERR;
  }
  // Adjust input parameters for execvp() (we do this even if unnecessary)

  argv += input.nb_parsed_params;
  argc -= input.nb_parsed_params;

  // Only throw an error if we needed the user to pass an arg
  if (ctx.params.pid) {
    if (ctx.params.pid == -1)
      LG_NFO("Instrumenting whole system");
    else
      LG_NFO("Instrumenting PID %d", ctx.params.pid);
  } else if (argc <= 0) {
    LG_ERR("No target specified, exiting");
    goto CLEANUP_ERR;
  }

  /****************************************************************************\
  |                             Run the Profiler                               |
  \****************************************************************************/
  // If the profiler was disabled, just skip ahead
  if (!ctx.params.enable) {
    LG_NFO("Profiling disabled");
  } else {
    // Initialize profiling.
    // If no PID was specified earlier, we autodaemonize and launch command
    if (!ctx.params.pid) {
      ctx.params.pid = getpid();
      pid_t child_pid = fork();

      // child_pid 0 if child.  Fork again and return to autodaemonize
      if (!child_pid) {
        if (fork())
          goto CLEANUP; // Intermediate child returns, grandchild profiles
      } else {
        usleep(100000);
        goto EXECUTE;
      }
      waitpid(child_pid, NULL, 0);
    }

    // Attach the profiler
    ddprof_attach_profiler(&ctx);
    LG_WRN("Profiling terminated");
    goto CLEANUP; // todo : propagate errors
  }

EXECUTE:
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

CLEANUP_ERR:
  ret = -1;
CLEANUP:
  ddprof_ctx_free(&ctx);
CLEANUP_INPUT:
  // These are cleaned by execvp(), but we remove them here since this is the
  // error path and we don't want static analysis to report leaks.
  ddprof_input_free(&input);
  return ret;
}
