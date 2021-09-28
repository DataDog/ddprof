#include "ddprof.h"

#include <execinfo.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <unistd.h>
#include <x86intrin.h>

#include "cap_display.h"
#include "ddprof_cmdline.h"
#include "ddprof_context.h"
#include "ddprof_input.h"
#include "ddprof_stats.h"
#include "ddprof_worker.h"
#include "ddres.h"
#include "logger.h"
#include "perf_mainloop.h"
#include "pevent_lib.h"
#include "procutils.h"
#include "version.h"

void ddprof_ctx_free(DDProfContext *ctx) {
  exporter_input_free(&ctx->exp_input);
  free((char *)ctx->params.internalstats);
}

/*****************************  SIGSEGV Handler *******************************/
static void sigsegv_handler(int sig, siginfo_t *si, void *uc) {
  // TODO this really shouldn't call printf-family functions...
  (void)uc;
  static void *buf[4096] = {0};
  size_t sz = backtrace(buf, 4096);
  fprintf(stderr, "ddprof[%d]: <%s> has encountered an error and will exit\n",
          getpid(), str_version().ptr);
  if (sig == SIGSEGV)
    printf("[DDPROF] Fault address: %p\n", si->si_addr);
  backtrace_symbols_fd(buf, sz, STDERR_FILENO);
  exit(-1);
}

static DDRes ddprof_setup(DDProfContext *ctx, pid_t pid, int num_cpu) {
  PEventHdr *pevent_hdr = &ctx->worker_ctx.pevent_hdr;
  pevent_init(pevent_hdr);

  // Don't stop if error as this is only for debug purpose
  if (IsDDResNotOK(log_capabilities(false))) {
    LG_ERR("Error when printing capabilities, continuing...");
  }

  DDRES_CHECK_FWD(pevent_open(ctx, pid, num_cpu, pevent_hdr));

  // Setup signal handler if defined
  if (ctx->params.faultinfo)
    sigaction(SIGSEGV,
              &(struct sigaction){.sa_sigaction = sigsegv_handler,
                                  .sa_flags = SA_SIGINFO},
              NULL);

  // Disable core dumps (unless enabled)
  if (!ctx->params.coredumps)
    setrlimit(RLIMIT_CORE, 0);

  // Set the nice level, but only if it was overridden because 0 is valid
  if (ctx->params.nice != -1) {
    setpriority(PRIO_PROCESS, 0, ctx->params.nice);
    if (errno) {
      LG_WRN("Requested nice level (%d) could not be set", ctx->params.nice);
    }
  }

  DDRES_CHECK_FWD(ddprof_stats_init());

  DDRES_CHECK_FWD(pevent_enable(pevent_hdr));

  return ddres_init();
}

static DDRes ddprof_breakdown(DDProfContext *ctx) {
  PEventHdr *pevent_hdr = &ctx->worker_ctx.pevent_hdr;

  if (IsDDResNotOK(pevent_cleanup(pevent_hdr))) {
    LG_WRN("Error when calling pevent_cleanup.");
  }

  DDRES_CHECK_FWD(ddprof_stats_free());

  return ddres_init();
}

#ifndef DDPROF_NATIVE_LIB
/*************************  Instrumentation Helpers  **************************/
void ddprof_attach_profiler(DDProfContext *ctx, int num_cpu) {

  pid_t pid = ctx->params.pid;
  const WorkerAttr perf_funs = {
      .init_fun = ddprof_worker_init,
      .finish_fun = ddprof_worker_finish,
  };
  if (IsDDResNotOK(ddprof_setup(ctx, pid, num_cpu))) {
    LG_ERR("Error seting up ddprof.");
    return;
  }

  LG_NTC("Entering main loop");
  // Enter the main loop -- this will not return unless there is an error.
  main_loop(&perf_funs, ctx);
  // If we're here, the main loop closed--probably the profilee closed
  if (errno)
    LG_WRN("Profiling context no longer valid (%s)", strerror(errno));
  else
    LG_WRN("Profiling context no longer valid");

  if (IsDDResNotOK(ddprof_breakdown(ctx)))
    LG_ERR("Error when calling ddprof_breakdown");
  return;
}
#endif

void ddprof_attach_handler(DDProfContext *ctx,
                           const StackHandler *stack_handler, int num_cpu) {
  const WorkerAttr perf_funs = {
      .init_fun = worker_unwind_init,
      .finish_fun = worker_unwind_free,
  };
  pid_t pid = ctx->params.pid;

  if (IsDDResNotOK(ddprof_setup(ctx, pid, num_cpu))) {
    LG_ERR("Error setting up ddprof");
    return;
  }
  // User defined handler
  ctx->stack_handler = stack_handler;
  // Enter the main loop -- returns after a number of cycles.
  LG_PRINT("Initiating Profiling");
  main_loop_lib(&perf_funs, ctx);
  if (errno)
    LG_WRN("Profiling context no longer valid (%s)", strerror(errno));
  else
    LG_WRN("Profiling context no longer valid");

  if (IsDDResNotOK(ddprof_breakdown(ctx)))
    LG_ERR("Error when calling ddprof_breakdown.");
  return;
}

/****************************  Argument Processor  ***************************/
DDRes ddprof_ctx_set(const DDProfInput *input, DDProfContext *ctx) {
  memset(ctx, 0, sizeof(DDProfContext));
  // Process logging mode
  char const *logpattern[] = {"stdout", "stderr", "syslog", "disabled"};
  const int sizeOfLogpattern = 4;
  switch (arg_which(input->logmode, logpattern, sizeOfLogpattern)) {
  case -1:
  case 0:
    LOG_open(LOG_STDOUT, "");
    break;
  case 1:
    LOG_open(LOG_STDERR, "");
    break;
  case 2:
    LOG_open(LOG_SYSLOG, "");
    break;
  case 3:
    LOG_open(LOG_DISABLE, "");
    break;
  default:
    LOG_open(LOG_FILE, input->logmode);
    break;
  }

  // Process logging level
  char const *loglpattern[] = {"debug", "notice", "warn", "error"};
  const int sizeOfLoglpattern = 4;
  switch (arg_which(input->loglevel, loglpattern, sizeOfLoglpattern)) {
  case 0:
    LOG_setlevel(LL_DEBUG);
    break;
  case 1:
    LOG_setlevel(LL_NOTICE);
    break;
  case -1:
  case 2:
    LOG_setlevel(LL_WARNING);
    break;
  case 3:
    LOG_setlevel(LL_ERROR);
    break;
  }

  for (int idx_watcher = 0; idx_watcher < input->num_watchers; ++idx_watcher) {
    ctx->watchers[ctx->num_watchers] =
        *(perfoptions_preset(input->watchers[idx_watcher]));
    if (input->sampling_value[idx_watcher]) // override preset
      ctx->watchers[ctx->num_watchers].sample_period =
          input->sampling_value[idx_watcher];
  }
  ctx->num_watchers = input->num_watchers;

  // If events are set, install default watcher
  if (!ctx->num_watchers) {
    ctx->num_watchers = 1;
    ctx->watchers[0] = *perfoptions_preset(10);
  }

  DDRES_CHECK_FWD(exporter_input_copy(&input->exp_input, &ctx->exp_input));

  // Set defaults
  ctx->params.enable = true;
  ctx->params.upload_period = 60.0;

  // Process enable.  Note that we want the effect to hit an inner profile.
  // TODO das210603 do the semantics of this match other profilers?
  ctx->params.enable = !arg_yesno(input->enable, 0); // default yes
  if (ctx->params.enable)
    setenv("DD_PROFILING_ENABLED", "true", true);
  else
    setenv("DD_PROFILING_ENABLED", "false", true);

  // Process native profiler enablement override
  ctx->params.enable = !arg_yesno(input->native_enable, 0);

  // process upload_period
  if (input->upload_period) {
    double x = strtod(input->upload_period, NULL);
    if (x > 0.0)
      ctx->params.upload_period = x;
  }

  ctx->params.worker_period = 240;
  if (input->worker_period) {
    char *ptr_period = input->worker_period;
    int tmp_period = strtol(input->worker_period, &ptr_period, 10);
    if (ptr_period != input->worker_period && tmp_period > 0)
      ctx->params.worker_period = tmp_period;
  }

  // process cache_period
  // NOTE: if cache_period > worker_period, we will never reset the cache.
  ctx->params.cache_period = 15;
  if (input->cache_period) {
    char *ptr_period = input->cache_period;
    int tmp_period = strtol(input->cache_period, &ptr_period, 10);
    if (ptr_period != input->cache_period && tmp_period > 0)
      ctx->params.cache_period = tmp_period;
  }

  // Process faultinfo
  ctx->params.faultinfo = arg_yesno(input->faultinfo, 1); // default no

  // Process coredumps
  // This probably makes no sense with faultinfo enabled, but considering that
  // there are other dumpable signals, we ignore
  ctx->params.coredumps = arg_yesno(input->coredumps, 1); // default no

  // Process nice level
  // default value is -1 : nothing to override
  ctx->params.nice = -1;
  if (input->nice) {
    char *ptr_nice = input->nice;
    int tmp_nice = strtol(input->nice, &ptr_nice, 10);
    if (ptr_nice != input->nice)
      ctx->params.nice = tmp_nice;
  }

  // Process sendfinal
  ctx->params.sendfinal = arg_yesno(input->sendfinal, 1);

  // Adjust target PID
  pid_t pid_tmp = 0;
  if (input->pid && (pid_tmp = strtol(input->pid, NULL, 10)))
    ctx->params.pid = pid_tmp;

  // Adjust global mode
  ctx->params.global = arg_yesno(input->global, 1); // default no
  if (ctx->params.global) {
    if (ctx->params.pid) {
      LG_WRN("[INPUT] Ignoring PID (%d) in param due to global mode",
             ctx->params.pid);
    }
    ctx->params.pid = -1;
  }

  if (input->internalstats) {
    ctx->params.internalstats = strdup(input->internalstats);
    if (!ctx->params.internalstats) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC,
                             "Unable to allocate string for internalstats");
    }
  }

  // Process input printer (do this right before argv/c modification)
  if (input->printargs && arg_yesno(input->printargs, 1)) {
    LG_PRINT("Printing parameters -->");
    ddprof_print_params(input);

    LG_PRINT("  Native profiler enabled: %s",
             ctx->params.enable ? "true" : "false");

    // Tell the user what mode is being used
    LG_PRINT("  Profiling mode: %s",
             -1 == ctx->params.pid ? "global"
                 : pid_tmp         ? "target"
                                   : "wrapper");

    // Show watchers
    LG_PRINT("  Instrumented with %d watchers:", ctx->num_watchers);
    for (int i = 0; i < ctx->num_watchers; i++) {
      LG_PRINT("    ID: %s, Pos: %d, Index: %lu, Label: %s, Mode: %d",
               ctx->watchers[i].desc, i, ctx->watchers[i].config,
               ctx->watchers[i].label, ctx->watchers[i].mode);
    }
  }
  return ddres_init();
}
