// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof.h"

#include <errno.h>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <x86intrin.h>

#include "cap_display.h"
#include "ddprof_cmdline.h"
#include "ddprof_context.h"
#include "ddprof_context_lib.h"
#include "ddprof_input.h"
#include "ddprof_stats.h"
#include "ddprof_worker.h"
#include "ddres.h"
#include "logger.h"
#include "perf_mainloop.h"
#include "pevent_lib.h"
#include "version.h"

static void disable_core_dumps(void) {
  struct rlimit core_limit;
  core_limit.rlim_cur = 0;
  core_limit.rlim_max = 0;
  setrlimit(RLIMIT_CORE, &core_limit);
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

DDRes ddprof_setup(DDProfContext *ctx, pid_t pid) {
  PEventHdr *pevent_hdr = &ctx->worker_ctx.pevent_hdr;
  pevent_init(pevent_hdr);

  // Don't stop if error as this is only for debug purpose
  if (IsDDResNotOK(log_capabilities(false))) {
    LG_ERR("Error when printing capabilities, continuing...");
  }

  DDRES_CHECK_FWD(pevent_open(ctx, pid, ctx->params.num_cpu, pevent_hdr));

  // Setup signal handler if defined
  if (ctx->params.faultinfo)
    sigaction(SIGSEGV,
              &(struct sigaction){.sa_sigaction = sigsegv_handler,
                                  .sa_flags = SA_SIGINFO},
              NULL);

  // Disable core dumps (unless enabled)
  if (!ctx->params.coredumps) {
    disable_core_dumps();
  }

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
void ddprof_start_profiler(DDProfContext *ctx) {
  const WorkerAttr perf_funs = {
      .init_fun = ddprof_worker_init,
      .finish_fun = ddprof_worker_finish,
  };

  // Enter the main loop -- this will not return unless there is an error.
  LG_PRINT("Entering main loop");
  main_loop(&perf_funs, ctx);

  // If we're here, the main loop closed--probably the profilee closed
  if (errno)
    LG_WRN("Profiling context no longer valid (%s)", strerror(errno));
  else
    LG_NTC("Profiling context no longer valid");

  if (IsDDResNotOK(ddprof_breakdown(ctx)))
    LG_WRN("Error when calling ddprof_breakdown");
  return;
}
#endif

void ddprof_attach_handler(DDProfContext *ctx,
                           const StackHandler *stack_handler) {
  const WorkerAttr perf_funs = {
      .init_fun = worker_unwind_init,
      .finish_fun = worker_unwind_free,
  };
  pid_t pid = ctx->params.pid;

  if (IsDDResNotOK(ddprof_setup(ctx, pid))) {
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
