// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_context_lib.h"

#include "ddprof_cmdline.h"
#include "ddprof_context.h"
#include "ddprof_input.h"
#include "logger.h"

#include <sys/sysinfo.h>

/****************************  Argument Processor  ***************************/
DDRes ddprof_context_set(const DDProfInput *input, DDProfContext *ctx) {
  memset(ctx, 0, sizeof(DDProfContext));
  // Process logging mode
  char const *logpattern[] = {"stdout", "stderr", "syslog", "disabled"};
  const int sizeOfLogpattern = 4;
  int idx_log_mode = input->logmode
      ? arg_which(input->logmode, logpattern, sizeOfLogpattern)
      : 0; // default to stdout
  switch (idx_log_mode) {
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
  char const *loglpattern[] = {"debug", "informational", "notice", "warn",
                               "error"};
  const int sizeOfLoglpattern = 5;
  switch (arg_which(input->loglevel, loglpattern, sizeOfLoglpattern)) {
  case 0:
    LOG_setlevel(LL_DEBUG);
    break;
  case 1:
    LOG_setlevel(LL_INFORMATIONAL);
    break;
  case 2:
    LOG_setlevel(LL_NOTICE);
    break;
  case -1: // default
  case 3:
    LOG_setlevel(LL_WARNING);
    break;
  case 4:
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

  ctx->params.num_cpu = get_nprocs();

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
  if (input->tags) {
    ctx->params.tags = strdup(input->tags);
    if (!ctx->params.tags) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC,
                             "Unable to allocate string for tags");
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

void ddprof_context_free(DDProfContext *ctx) {
  exporter_input_free(&ctx->exp_input);
  free((char *)ctx->params.internalstats);
  free((char *)ctx->params.tags);
}