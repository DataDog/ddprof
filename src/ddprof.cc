// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof.hpp"

#include <cerrno>

#include <csignal>
#include <cstdio>
#include <sys/resource.h>
#include <unistd.h>

#include "cap_display.hpp"
#include "ddprof_cmdline.hpp"
#include "ddprof_context.hpp"
#include "ddprof_context_lib.hpp"
#include "ddprof_stats.hpp"
#include "ddprof_worker.hpp"
#include "ddres.hpp"
#include "logger.hpp"
#include "perf_mainloop.hpp"
#include "pevent_lib.hpp"
#include "sys_utils.hpp"
#include "version.hpp"

#ifdef __GLIBC__
#  include <execinfo.h>
#endif

namespace ddprof {
namespace {
/*****************************  SIGSEGV Handler *******************************/
void sigsegv_handler(int sig, siginfo_t *si, void *uc) {
  // TODO this really shouldn't call printf-family functions...
  (void)uc;
#ifdef __GLIBC__
  constexpr size_t k_stacktrace_buffer_size = 4096;
  static void *buf[k_stacktrace_buffer_size] = {};
  size_t const sz = backtrace(buf, std::size(buf));
#endif
  (void)fprintf(
      stderr, "ddprof[%d]: <%.*s> has encountered an error and will exit\n",
      getpid(), static_cast<int>(str_version().size()), str_version().data());
  if (sig == SIGSEGV) {
    printf("[DDPROF] Fault address: %p\n", si->si_addr);
  }
#ifdef __GLIBC__
  backtrace_symbols_fd(buf, sz, STDERR_FILENO);
#endif
  exit(-1);
}

void display_system_info() {

  // Don't stop if error as this is only for debug purpose
  if (IsDDResNotOK(log_capabilities(false))) {
    LG_ERR("Error when printing capabilities, continuing...");
  }
  int val;
  if (IsDDResOK(sys_perf_event_paranoid(val))) {
    LG_NFO("perf_event_paranoid : %d", val);
  } else {
    LG_WRN("Unable to access perf_event_paranoid setting");
  }
}
} // namespace

DDRes ddprof_setup(DDProfContext &ctx) {
  PEventHdr *pevent_hdr = &ctx.worker_ctx.pevent_hdr;
  try {
    pevent_init(pevent_hdr);

    display_system_info();

    // Open perf events and mmap events right now to start receiving events
    // mmaps from perf fds will be lost after fork, that why we mmap them again
    // in worker (but kernel only accounts for the pinned memory once).
    DDRES_CHECK_FWD(
        pevent_setup(ctx, ctx.params.pid, ctx.params.num_cpu, pevent_hdr));

    // Setup signal handler if defined
    if (ctx.params.fault_info) {
      struct sigaction sigaction_handlers = {};
      sigaction_handlers.sa_sigaction = sigsegv_handler;
      sigaction_handlers.sa_flags = SA_SIGINFO;
      sigaction(SIGSEGV, &(sigaction_handlers), nullptr);
    }

    // Set the nice level, but only if it was overridden because 0 is valid
    if (ctx.params.nice != -1) {
      setpriority(PRIO_PROCESS, 0, ctx.params.nice);
      if (errno) {
        LG_WRN("Requested nice level (%d) could not be set", ctx.params.nice);
      }
      else {
        LG_NTC("Success overriding nice level (%d)", ctx.params.nice);
      }
    }

    DDRES_CHECK_FWD(ddprof_stats_init());

    DDRES_CHECK_FWD(pevent_enable(pevent_hdr));
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes ddprof_teardown(DDProfContext &ctx) {
  PEventHdr *pevent_hdr = &ctx.worker_ctx.pevent_hdr;

  if (IsDDResNotOK(pevent_cleanup(pevent_hdr))) {
    LG_WRN("Error when calling pevent_cleanup.");
  }

  if (IsDDResNotOK(ddprof_stats_free())) {
    LG_WRN("Error when calling ddprof_stats_free.");
  }

  return ddres_init();
}

/*************************   Instrumentation Helpers **************************/
DDRes ddprof_start_profiler(DDProfContext *ctx) {
  const WorkerAttr perf_funs = {
      .init_fun = ddprof_worker_init,
      .finish_fun = ddprof_worker_free,
  };

  // Enter the main loop -- this will not return unless there is an error.
  LG_NFO("Entering main loop");
  return main_loop(&perf_funs, ctx);
}
} // namespace ddprof
