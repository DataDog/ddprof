#include "ddprof.h"

#include <errno.h>
#include <getopt.h>
#include <libelf.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>
#include <wait.h>

int main(int argc, char **argv) {
  //---- Inititiate structs
  int c = 0, oi = 0, ret = 0;
  DDProfContext *ctx = ddprof_ctx_init();

  struct option lopts[] = {OPT_TABLE(X_LOPT){"event", 1, 0, 'e'},
                           {"help", 0, 0, 'h'},
                           {"version", 0, 0, 'v'}};

  // Early exit if the user just ran the bare command
  if (argc <= 1) {
    print_help();
    return 0;
  }

  //---- Process Options
  // Populate default values (mutates ctx)
  OPT_TABLE(X_DFLT);

  char opt_short[] = "+" OPT_TABLE(X_OSTR) "e:hv";
  while (-1 != (c = getopt_long(argc, argv, opt_short, lopts, &oi))) {
    switch (c) {
      OPT_TABLE(X_CASE)
    case 'e':
      if (!ddprof_ctx_watcher_process(ctx, optarg))
        LG_WRN("Ignoring invalid event (%s)", optarg);
      break;
    case 'h':;
      OPT_TABLE(X_FREE);
      print_help();
      return 0;
    case 'v':;
      OPT_TABLE(X_FREE);
      print_version();
      return 0;
    default:;
      OPT_TABLE(X_FREE);
      LG_ERR("Invalid option %c", c);
      return -1;
    }
  }

  // cmdline args have been processed.  Set the ctx
  ddprof_setctx(ctx);

  // Adjust input parameters for execvp() (we do this even if unnecessary)
  argv += optind;
  argc -= optind;

  // Only throw an error if we needed the user to pass an arg
  if (ctx->params.pid) {
    if (ctx->params.pid == -1)
      LG_NTC("Instrumenting whole system");
    else
      LG_NTC("Instrumenting PID %d", ctx->params.pid);
  } else if (argc <= 0) {
    LG_ERR("No target specified, exiting");
    goto CLEANUP_ERR;
  }

  /****************************************************************************\
  |                             Run the Profiler                               |
  \****************************************************************************/
  // If the profiler was disabled, just skip ahead
  if (!ctx->params.enable) {
    LG_WRN("Profiling disabled");
  } else {
    // Initialize the request object
    DDR_init(ctx->ddr);

    // Initialize the pprof
    char *pprof_labels[max_watchers];
    char *pprof_units[max_watchers];
    for (int i = 0; i < ctx->num_watchers; i++) {
      pprof_labels[i] = ctx->watchers[i].label;
      pprof_units[i] = ctx->watchers[i].unit;
    }
    if (!pprof_Init(ctx->dp, (const char **)pprof_labels,
                    (const char **)pprof_units, ctx->num_watchers)) {
      LG_ERR("Failed to initialize profiling storage, profiling disabled");

      // If we were going to run the command, then run it now
      if (!ctx->params.pid)
        goto EXECUTE;
    }
    pprof_timeUpdate(ctx->dp); // Set the time

    // Initialize profiling.
    // If no PID was specified earlier, we autodaemonize and launch command
    if (!ctx->params.pid) {
      ctx->params.pid = getpid();
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
    instrument_pid(ctx, ctx->params.pid, get_nprocs());
    LG_WRN("Profiling terminated");
    goto CLEANUP_ERR;
  }

EXECUTE:
  if (-1 == execvp(*argv, argv)) {
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
  // These are cleaned by execvp(), but we remove them here since this is the
  // error path and we don't want static analysis to report leaks.
  OPT_TABLE(X_FREE);
  ddprof_ctx_free(ctx);
  return ret;
}
