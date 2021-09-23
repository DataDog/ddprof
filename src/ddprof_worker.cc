extern "C" {
#include "ddprof_worker.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <x86intrin.h>

#include "ddprof_context.h"
#include "ddprof_stats.h"
#include "dso.h"
#include "exporter/ddprof_exporter.h"
#include "logger.h"
#include "perf.h"
#include "pevent_lib.h"
#include "pprof/ddprof_pprof.h"
#include "stack_handler.h"
#include "unwind.h"
#include "unwind_output.h"
}

#include "dso.hpp"

#include <cassert>

#ifdef DBG_JEMALLOC
#  include <jemalloc/jemalloc.h>
#endif

using ddprof::DsoStats;

static const DDPROF_STATS s_cycled_stats[] = {
    STATS_UNWIND_TICKS, STATS_EVENT_LOST, STATS_SAMPLE_COUNT,
    STATS_DSO_UNHANDLED_SECTIONS};

#define cycled_stats_sz (sizeof(s_cycled_stats) / sizeof(DDPROF_STATS))

/// Human readable runtime information
static void print_diagnostics(const DsoHdr *dso_hdr) {
  ddprof_stats_print();
  dso_hdr->_stats.log();
#ifdef DBG_JEMALLOC
  // jemalloc stats
  malloc_stats_print(NULL, NULL, "");
#endif
}

static inline int64_t now_nanos() {
  static struct timeval tv = {0};
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000000 + tv.tv_usec) * 1000;
}

static inline long export_time_convert(double upload_period) {
  return upload_period * 1000000000;
}

static inline void export_time_set(DDProfContext *ctx) {
  assert(ctx);
  ctx->worker_ctx.send_nanos =
      now_nanos() + export_time_convert(ctx->params.upload_period);
}

DDRes worker_unwind_init(DDProfContext *ctx) {

  // Set the initial time
  export_time_set(ctx);
  // Make sure worker-related counters are reset
  ctx->worker_ctx.count_worker = 0;
  ctx->worker_ctx.count_cache = 0;

  ctx->worker_ctx.us = (UnwindState *)calloc(1, sizeof(UnwindState));
  if (!ctx->worker_ctx.us) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC,
                           "Error when creating unwinding state");
  }

  PEventHdr *pevent_hdr = &ctx->worker_ctx.pevent_hdr;

  // If we're here, then we are a child spawned during the startup operation.
  // That means we need to iterate through the perf_event_open() handles and
  // get the mmaps
  DDRES_CHECK_FWD(pevent_mmap(pevent_hdr));
  // Initialize the unwind state and library
  DDRES_CHECK_FWD(unwind_init(ctx->worker_ctx.us));
  return ddres_init();
}

DDRes worker_unwind_free(DDProfContext *ctx) {
  PEventHdr *pevent_hdr = &ctx->worker_ctx.pevent_hdr;
  unwind_free(ctx->worker_ctx.us);
  DDRES_CHECK_FWD(pevent_munmap(pevent_hdr));
  free(ctx->worker_ctx.us);
  return ddres_init();
}

/// Retrieve cpu / memory info
static DDRes worker_update_stats(ProcStatus *procstat, const DsoHdr *dso_hdr) {
  // Update the procstats, but first snapshot the utime so we can compute the
  // diff for the utime metric
  long utime_old = procstat->utime;
  DDRES_CHECK_FWD(proc_read(procstat));

  ddprof_stats_set(STATS_PROCFS_RSS, 1024 * procstat->rss);
  ddprof_stats_set(STATS_PROCFS_UTIME, procstat->utime - utime_old);
  ddprof_stats_set(STATS_DSO_UNHANDLED_SECTIONS,
                   dso_hdr->_stats.sum_event_metric(DsoStats::kUnhandledDso));
  ddprof_stats_set(STATS_DSO_NEW_DSO,
                   dso_hdr->_stats.sum_event_metric(DsoStats::kNewDso));
  ddprof_stats_set(STATS_DSO_SIZE, dso_hdr->_set.size());
  ddprof_stats_set(STATS_DSO_MAPPED, dso_hdr->_region_map.size());
  return ddres_init();
}

/************************* perf_event_open() helpers **************************/
/// Entry point for sample aggregation
DDRes ddprof_pr_sample(DDProfContext *ctx, perf_event_sample *sample, int pos) {
  // Before we do anything else, copy the perf_event_header into a sample
  struct UnwindState *us = ctx->worker_ctx.us;
  ddprof_stats_add(STATS_SAMPLE_COUNT, 1, NULL);
  us->pid = sample->pid;
  us->stack = NULL;
  us->stack_sz = sample->size_stack;
  us->stack = sample->data_stack;

  memcpy(&us->regs[0], sample->regs, PERF_REGS_COUNT * sizeof(uint64_t));
  uw_output_clear(&us->output);
  unsigned long this_ticks_unwind = __rdtsc();
  // Aggregate if unwinding went well
  if (IsDDResOK(unwindstate__unwind(us))) {
    DDRES_CHECK_FWD(ddprof_stats_add(STATS_UNWIND_TICKS,
                                     __rdtsc() - this_ticks_unwind, NULL));

    // in lib mode we don't aggregate (protect to avoid link failures)
#ifndef DDPROF_NATIVE_LIB
    DDProfPProf *pprof = ctx->worker_ctx.pprof;
    DDRES_CHECK_FWD(pprof_aggregate(&us->output, us->symbols_hdr,
                                    sample->period, pos, pprof));

#else
    if (ctx->stack_handler) {
      if (!ctx->stack_handler->apply(&us->output, ctx,
                                     ctx->stack_handler->callback_ctx, pos)) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_STACK_HANDLE,
                               "Stack handler returning errors");
      }
    }
#endif
  }

  return ddres_init();
}

static void ddprof_reset_worker_stats(DsoHdr *dso_hdr) {
  for (unsigned i = 0; i < cycled_stats_sz; ++i) {
    ddprof_stats_clear(s_cycled_stats[i]);
  }
  dso_hdr->_stats.reset();
}

/// Cycle operations : export, sync metrics, update counters
static DDRes ddprof_worker_cycle(DDProfContext *ctx, int64_t now) {

  // Scrape procfs for process usage statistics
  DDRES_CHECK_FWD(worker_update_stats(&ctx->worker_ctx.proc_status,
                                      ctx->worker_ctx.us->dso_hdr));

  // And emit diagnostic output (if it's enabled)
  print_diagnostics(ctx->worker_ctx.us->dso_hdr);
  DDRES_CHECK_FWD(ddprof_stats_send(ctx->params.internalstats));

#ifndef DDPROF_NATIVE_LIB
  // Take the current pprof contents and ship them to the backend.  This also
  // clears the pprof for reuse
  DDRES_CHECK_FWD(ddprof_exporter_export(ctx->worker_ctx.pprof->_profile,
                                         ctx->worker_ctx.exp));
  DDRES_CHECK_FWD(pprof_reset(ctx->worker_ctx.pprof));

#endif

  // Increase the counts of exports
  ctx->worker_ctx.count_worker += 1;
  ctx->worker_ctx.count_cache += 1;

  // allow new backpopulates
  ctx->worker_ctx.us->dso_hdr->reset_backpopulate_requests();

  // Update the time last sent
  ctx->worker_ctx.send_nanos += export_time_convert(ctx->params.upload_period);

  // If the clock was frozen for some reason, we need to detect situations
  // where we'll have catchup windows and reset the export timer.  This can
  // easily happen under temporary load when the profiler is off-CPU, if the
  // process is put in the cgroup freezer, or if we're being emulated.
  if (now > ctx->worker_ctx.send_nanos) {
    LG_WRN("Timer skew detected; frequent warnings may suggest system issue");
    export_time_set(ctx);
  }

  // Reset stats relevant to a single cycle
  ddprof_reset_worker_stats(ctx->worker_ctx.us->dso_hdr);

  return ddres_init();
}

void ddprof_pr_mmap(DDProfContext *ctx, perf_event_mmap *map, int pos) {
  (void)ctx;
  if (!(map->header.misc & PERF_RECORD_MISC_MMAP_DATA)) {
    LG_DBG("[PERF]<%d>(MAP)%d: %s (%lx/%lx/%lx)", pos, map->pid, map->filename,
           map->addr, map->len, map->pgoff);
    ddprof::Dso new_dso(map->pid, map->addr, map->addr + map->len - 1,
                        map->pgoff, std::string(map->filename));
    ctx->worker_ctx.us->dso_hdr->insert_erase_overlap(std::move(new_dso));
  }
}

void ddprof_pr_lost(DDProfContext *ctx, perf_event_lost *lost, int pos) {
  ctx->worker_ctx.us->dso_hdr->reset_backpopulate_requests();
  (void)pos;
  ddprof_stats_add(STATS_EVENT_LOST, lost->lost, NULL);
}

void ddprof_pr_comm(DDProfContext *ctx, perf_event_comm *comm, int pos) {
  (void)ctx;
  // Change in process name (assuming exec) : clear all associated dso
  if (comm->header.misc & PERF_RECORD_MISC_COMM_EXEC) {
    LG_DBG("[PERF]<%d>(COMM)%d -> %s", pos, comm->pid, comm->comm);
    ctx->worker_ctx.us->dso_hdr->pid_free(comm->pid);
  }
}

void ddprof_pr_fork(DDProfContext *ctx, perf_event_fork *frk, int pos) {
  (void)ctx;
  LG_DBG("[PERF]<%d>(FORK)%d -> %d", pos, frk->ppid, frk->pid);
  if (frk->ppid != frk->pid) {
    ctx->worker_ctx.us->dso_hdr->pid_fork(frk->ppid, frk->pid);
  }
}

void ddprof_pr_exit(DDProfContext *ctx, perf_event_exit *ext, int pos) {
  (void)ctx;
  LG_DBG("[PERF]<%d>(EXIT)%d", pos, ext->pid);
  // Although pid dies, we do not know how many threads remain alive on this pid
  // (backpopulation will repopulate and fix things if needed)
}

/****************************** other functions *******************************/
static DDRes reset_state(DDProfContext *ctx,
                         volatile bool *continue_profiling) {
  if (!ctx) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_UKNW, "[DDPROF] Invalid context in %s",
                           __FUNCTION__);
  }

  // Check to see whether we need to clear the whole worker
  // NOTE: we do not reset the counters here, since clearing the worker
  //       1. is nonlocalized to this function; we just send a return value
  //          which informs the caller to refresh the worker
  //       2. new worker should be initialized with a fresh state, so clearing
  //          it here is irrelevant anyway
  if (ctx->params.worker_period <= ctx->worker_ctx.count_worker) {
    *continue_profiling = true;
    DDRES_RETURN_WARN_LOG(DD_WHAT_WORKER_RESET, "%s: cnt=%u - stop worker (%s)",
                          __FUNCTION__, ctx->worker_ctx.count_worker,
                          (*continue_profiling) ? "continue" : "stop");
  }

  // If we haven't hit the hard cap, have we hit the soft cap?
  if (ctx->params.cache_period <= ctx->worker_ctx.count_cache) {
    ctx->worker_ctx.count_cache = 0;
    DDRES_CHECK_FWD(dwfl_caches_clear(ctx->worker_ctx.us));
  }

  return ddres_init();
}

/********************************** callbacks *********************************/
DDRes ddprof_worker_timeout(volatile bool *continue_profiling,
                            DDProfContext *ctx) {
  int64_t now = now_nanos();
  if (now > ctx->worker_ctx.send_nanos) {
    DDRES_CHECK_FWD(ddprof_worker_cycle(ctx, now));
    // reset state defines if we should reboot the worker
    DDRes res = reset_state(ctx, continue_profiling);
    // A warning can be returned for a reset and should not be ignored
    if (IsDDResNotOK(res)) {
      return res;
    }
  }
  return ddres_init();
}

#ifndef DDPROF_NATIVE_LIB
DDRes ddprof_worker_init(DDProfContext *ctx) {

  ctx->worker_ctx.exp = (DDProfExporter *)malloc(sizeof(DDProfExporter));
  if (!ctx->worker_ctx.exp) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC, "Error when creating exporter");
  }
  ctx->worker_ctx.pprof = (DDProfPProf *)malloc(sizeof(DDProfPProf));
  if (!ctx->worker_ctx.pprof) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC,
                           "Error when creating pprof structure");
  }

  DDRES_CHECK_FWD(ddprof_exporter_init(&ctx->exp_input, ctx->worker_ctx.exp));
  DDRES_CHECK_FWD(ddprof_exporter_new(ctx->worker_ctx.exp));
  DDRES_CHECK_FWD(worker_unwind_init(ctx));
  DDRES_CHECK_FWD(pprof_create_profile(ctx->worker_ctx.pprof, ctx->watchers,
                                       ctx->num_watchers));
  return ddres_init();
}

DDRes ddprof_worker_finish(DDProfContext *ctx) {

  DDRES_CHECK_FWD(ddprof_exporter_free(ctx->worker_ctx.exp));
  DDRES_CHECK_FWD(worker_unwind_free(ctx));
  DDRES_CHECK_FWD(pprof_free_profile(ctx->worker_ctx.pprof));
  free(ctx->worker_ctx.pprof);
  free(ctx->worker_ctx.exp);
  return ddres_init();
}
#endif

DDRes ddprof_worker(struct perf_event_header *hdr, int pos,
                    volatile bool *continue_profiling, DDProfContext *ctx) {
  // global try catch to avoid leaking exceptions to main loop
  try {
    switch (hdr->type) {
    case PERF_RECORD_SAMPLE:
      DDRES_CHECK_FWD(ddprof_pr_sample(ctx, hdr2samp(hdr), pos));
      break;
    case PERF_RECORD_MMAP:
      ddprof_pr_mmap(ctx, (perf_event_mmap *)hdr, pos);
      break;
    case PERF_RECORD_LOST:
      ddprof_pr_lost(ctx, (perf_event_lost *)hdr, pos);
      break;
    case PERF_RECORD_COMM:
      ddprof_pr_comm(ctx, (perf_event_comm *)hdr, pos);
      break;
    case PERF_RECORD_EXIT:
      ddprof_pr_exit(ctx, (perf_event_exit *)hdr, pos);
      break;
    case PERF_RECORD_FORK:
      ddprof_pr_fork(ctx, (perf_event_fork *)hdr, pos);
      break;
    default:
      break;
    }
    // backpopulate if needed
    ctx->worker_ctx.us->dso_hdr->process_backpopulate_requests();

    // Click the timer at the end of processing, since we always add the
    // sampling rate to the last time.
    int64_t now = now_nanos();

    if (now > ctx->worker_ctx.send_nanos) {
      DDRES_CHECK_FWD(ddprof_worker_cycle(ctx, now));
      // reset state defines if we should reboot the worker
      DDRes res = reset_state(ctx, continue_profiling);
      // A warning can be returned for a reset and should not be ignored
      if (IsDDResNotOK(res)) {
        return res;
      }
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}
