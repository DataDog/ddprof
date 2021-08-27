#include "ddprof.h"

#include <sys/ioctl.h>
#include <sys/resource.h>
#include <unistd.h>
#include <x86intrin.h>

#include "cap_display.h"
#include "ddprof_cmdline.h"
#include "ddprof_stats.h"
#include "ddprof_worker.h"
#include "ddres.h"
#include "perf_mainloop.h"
#include "pevent_lib.h"
#include "procutils.h"
#include "statsd.h"
#include "unwind.h"

// Helpers for exapnding the OPT_TABLE here
#define X_PRNT(a, b, c, d, e, f, g, h)                                         \
  if ((f)->b)                                                                  \
    LG_NTC("  " #b ": %s", (f)->b);

#define USERAGENT_DEFAULT "libddprof"
#define LANGUAGE_DEFAULT "native"
#define FAMILY_DEFAULT "native"

bool ddprof_ctx_init(DDProfContext *ctx) {
  if (!ctx->ddr)
    return false;
  memset(ctx->ddr, 0, sizeof(*ctx->ddr));

  if (!ctx->dp)
    return false;
  memset(ctx->dp, 0, sizeof(*ctx->dp));

  if (!ctx->us)
    return false;
  memset(ctx->us, 0, sizeof(*ctx->us));

  ctx->ddr->user_agent = USERAGENT_DEFAULT;
  ctx->ddr->language = LANGUAGE_DEFAULT;
  ctx->ddr->family = FAMILY_DEFAULT;
  return true;
}

void ddprof_ctx_free(DDProfContext *ctx) {
  DDR_free(ctx->ddr);
  pprof_Free(ctx->dp);
}

/*****************************  SIGSEGV Handler *******************************/
void sigsegv_handler(int sig, siginfo_t *si, void *uc) {
  // TODO this really shouldn't call printf-family functions...
  (void)uc;
  static void *buf[4096] = {0};
  size_t sz = backtrace(buf, 4096);
  fprintf(stderr, "ddprof[%d]: <%s> has encountered an error and will exit\n",
          getpid(), str_version());
  if (sig == SIGSEGV)
    printf("[DDPROF] Fault address: %p\n", si->si_addr);
  backtrace_symbols_fd(buf, sz, STDERR_FILENO);
  exit(-1);
}

/*************************  Instrumentation Helpers  **************************/
void instrument_pid(DDProfContext *ctx, pid_t pid, int num_cpu) {
  perfopen_attr perf_funs = {.init_fun = ddprof_worker_init,
                             .finish_fun = ddprof_worker_finish,
                             .msg_fun = ddprof_worker,
                             .timeout_fun = ddprof_worker_timeout};
  PEventHdr pevent_hdr;
  pevent_init(&pevent_hdr);

  // Don't stop if error as this is only for debug purpose
  if (IsDDResNotOK(log_capabilities(false))) {
    LG_ERR("Error when printing capabilities, continuing...");
  }

  if (IsDDResNotOK(pevent_open(ctx, pid, num_cpu, &pevent_hdr))) {
    LG_ERR("Error when attaching to perf_event buffers.");
    return;
  }

  LG_NTC("Entering main loop");
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
  if (ctx->nice) {
    setpriority(PRIO_PROCESS, 0, ctx->params.nice);
    if (errno) {
      LG_WRN("Requested nice level (%d) could not be set", ctx->params.nice);
    }
  }

  if (IsDDResFatal(ddprof_stats_init(ctx->internalstats))) {
    LG_WRN("Error from statsd_init");
    return;
  }

  if (IsDDResNotOK(pevent_enable(&pevent_hdr))) {
    LG_ERR("Error when enabling watchers");
    return;
  }

  // Enter the main loop -- this will not return unless there is an error.
  main_loop(&pevent_hdr, &perf_funs, ctx);

  // If we're here, the main loop closed--probably the profilee closed
  if (errno)
    LG_WRN("Profiling context no longer valid (%s)", strerror(errno));
  else
    LG_WRN("Profiling context no longer valid");

  if (IsDDResNotOK(pevent_cleanup(&pevent_hdr))) {
    LG_ERR("Error when calling pevent_cleanup.");
  }

  if (IsDDResFatal(ddprof_stats_free())) {
    LG_ERR("Error from ddprof_stats_free");
    return;
  }
}

/****************************  Argument Processor  ***************************/
void ddprof_setctx(DDProfContext *ctx) {
  // If events are set, install default watcher
  if (!ctx->num_watchers) {
    ctx->num_watchers = 1;
    ctx->watchers[0] = *perfoptions_preset(10);
  }

  // Set defaults
  ctx->params.enable = true;
  ctx->params.upload_period = 60.0;

  // Process enable.  Note that we want the effect to hit an inner profile.
  // TODO das210603 do the semantics of this match other profilers?
  ctx->params.enable = !arg_yesno(ctx->enable, 0); // default yes
  if (ctx->params.enable)
    setenv("DD_PROFILING_ENABLED", "true", true);
  else
    setenv("DD_PROFILING_ENABLED", "false", true);

  // Process native profiler enablement override
  ctx->params.enable = !arg_yesno(ctx->native_enable, 0);

  // process upload_period
  if (ctx->upload_period) {
    double x = strtod(ctx->upload_period, NULL);
    if (x > 0.0)
      ctx->params.upload_period = x;
  }

  // process worker_period
  ctx->params.worker_period = 240;
  if (ctx->worker_period) {
    char *ptr_period = ctx->worker_period;
    int tmp_period = strtol(ctx->worker_period, &ptr_period, 10);
    if (ptr_period != ctx->worker_period && tmp_period > 0)
      ctx->params.worker_period = tmp_period;
  }

  // process cache_period
  // NOTE: we don't do anything to protect the scenario where cache_period >
  //       worker_period, even though the former clobbers the latter.
  ctx->params.cache_period = 15;
  if (ctx->cache_period) {
    char *ptr_period = ctx->cache_period;
    int tmp_period = strtol(ctx->cache_period, &ptr_period, 10);
    if (ptr_period != ctx->cache_period && tmp_period > 0)
      ctx->params.cache_period = tmp_period;
  }

  // Process faultinfo
  ctx->params.faultinfo = arg_yesno(ctx->faultinfo, 1); // default no

  // Process coredumps
  // This probably makes no sense with faultinfo enabled, but considering that
  // there are other dumpable signals, we ignore
  ctx->params.coredumps = arg_yesno(ctx->coredumps, 1); // default no

  // Process nice level
  if (ctx->nice) {
    char *ptr_nice = ctx->nice;
    int tmp_nice = strtol(ctx->nice, &ptr_nice, 10);
    if (ptr_nice != ctx->nice)
      ctx->params.nice = tmp_nice;
  }

  // Process sendfinal
  ctx->params.sendfinal = arg_yesno(ctx->sendfinal, 1);

  // Process logging mode
  char const *logpattern[] = {"stdout", "stderr", "syslog", "disabled"};
  const int sizeOfLogpattern = 4;
  switch (arg_which(ctx->logmode, logpattern, sizeOfLogpattern)) {
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
    LOG_open(LOG_FILE, ctx->logmode);
    break;
  }

  // Process logging level
  char const *loglpattern[] = {"debug", "notice", "warn", "error"};
  const int sizeOfLoglpattern = 4;
  switch (arg_which(ctx->loglevel, loglpattern, sizeOfLoglpattern)) {
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

  // process count_samples
  ctx->params.count_samples = arg_yesno(ctx->count_samples, 1); // default no

  // Adjust target PID
  pid_t pid_tmp = 0;
  if (ctx->pid && (pid_tmp = strtol(ctx->pid, NULL, 10)))
    ctx->params.pid = pid_tmp;

  // Adjust global mode
  ctx->params.global = arg_yesno(ctx->global, 1); // default no
  if (ctx->params.global)
    ctx->params.pid = -1;

  // Process input printer (do this right before argv/c modification)
  if (ctx->printargs && arg_yesno(ctx->printargs, 1)) {
    if (LOG_getlevel() < LL_DEBUG)
      LG_WRN("printarg specified, but loglevel too low to emit parameters");
    LG_DBG("Printing parameters");
    OPT_TABLE(X_PRNT);

    LG_DBG("Native profiler enabled: %s",
           ctx->params.enable ? "true" : "false");

    // Tell the user what mode is being used
    LG_DBG("Profiling mode: %s",
           -1 == ctx->params.pid ? "global"
               : pid_tmp         ? "target"
                                 : "wrapper");

    // Show watchers
    LG_DBG("Instrumented with %d watchers:", ctx->num_watchers);
    for (int i = 0; i < ctx->num_watchers; i++) {
      LG_DBG("  ID: %s, Pos: %d, Index: %lu, Label: %s, Mode: %d",
             ctx->watchers[i].desc, i, ctx->watchers[i].config,
             ctx->watchers[i].label, ctx->watchers[i].mode);
      LG_DBG("Done printing parameters");
    }
  }
}
