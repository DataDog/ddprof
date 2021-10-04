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
#include "logger.h"
#include "perf.h"
#include "pevent_lib.h"
#include "pprof/ddprof_pprof.h"
#include "stack_handler.h"
#include "unwind.h"
}

#include "dso.hpp"
#include "exporter/ddprof_exporter.h"
#include "tags.hpp"

#include <cassert>

#ifdef DBG_JEMALLOC
#  include <jemalloc/jemalloc.h>
#endif

using ddprof::DsoStats;

static const DDPROF_STATS s_cycled_stats[] = {STATS_UNWIND_TICKS,
                                              STATS_EVENT_COUNT,
                                              STATS_EVENT_LOST,
                                              STATS_SAMPLE_COUNT,
                                              STATS_DSO_UNHANDLED_SECTIONS,
                                              STATS_CPU_TIME};

#define cycled_stats_sz (sizeof(s_cycled_stats) / sizeof(DDPROF_STATS))

static const unsigned s_nb_samples_per_backpopulate = 200;

/// Human readable runtime information
static void print_diagnostics(const DsoHdr *dso_hdr) {
  LG_PRINT("Printing internal diagnostics");
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
  try {
    // Set the initial time
    export_time_set(ctx);
    // Make sure worker-related counters are reset
    ctx->worker_ctx.count_worker = 0;

    ctx->worker_ctx.us = (UnwindState *)calloc(1, sizeof(UnwindState));
    if (!ctx->worker_ctx.us) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC,
                             "Error when creating unwinding state");
    }

    PEventHdr *pevent_hdr = &ctx->worker_ctx.pevent_hdr;

    // If we're here, then we are a child spawned during the startup operation.
    // That means we need to iterate through the perf_event_open() handles and
    // get the mmaps
    if (!IsDDResOK(pevent_mmap(pevent_hdr, true))) {
      LG_NTC("Retrying attachment without user override");
      DDRES_CHECK_FWD(pevent_mmap(pevent_hdr, false));
    }
    // Initialize the unwind state and library
    DDRES_CHECK_FWD(unwind_init(ctx->worker_ctx.us));
    ctx->worker_ctx.user_tags =
        new UserTags(ctx->params.tags, ctx->params.num_cpu);
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes worker_unwind_free(DDProfContext *ctx) {
  try {
    delete ctx->worker_ctx.user_tags;
    ctx->worker_ctx.user_tags = nullptr;
    PEventHdr *pevent_hdr = &ctx->worker_ctx.pevent_hdr;
    unwind_free(ctx->worker_ctx.us);
    DDRES_CHECK_FWD(pevent_munmap(pevent_hdr));
    free(ctx->worker_ctx.us);
  }
  CatchExcept2DDRes();
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
  us->stack_sz = sample->size_stack;
  us->stack = sample->data_stack;

  // If this is a SW_TASK_CLOCK-type event, then aggregate the time
  if (ctx->watchers[pos].config == PERF_COUNT_SW_TASK_CLOCK)
    ddprof_stats_add(STATS_CPU_TIME, sample->period, NULL);

#ifdef DEBUG
  LG_DBG("[WORKER]<%d> (SAMPLE)%d: stack = %p / size = %lu", pos, us->pid,
         us->stack, us->stack_sz);
#endif
  memcpy(&us->regs[0], sample->regs, PERF_REGS_COUNT * sizeof(uint64_t));
  uw_output_clear(&us->output);
  unsigned long this_ticks_unwind = __rdtsc();
  DDRes res = unwindstate__unwind(us);

  // Aggregate if unwinding went well (todo : fatal error propagation)
  if (IsDDResOK(res)) {
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
  DDRES_CHECK_FWD(ddprof_stats_add(STATS_UNWIND_TICKS,
                                   __rdtsc() - this_ticks_unwind, NULL));

  return ddres_init();
}

static void ddprof_reset_worker_stats(DsoHdr *dso_hdr) {
  for (unsigned i = 0; i < cycled_stats_sz; ++i) {
    ddprof_stats_clear(s_cycled_stats[i]);
  }
  dso_hdr->_stats.reset();
  unwind_metrics_reset();
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

  // allow new backpopulates
  ctx->worker_ctx.us->dso_hdr->reset_backpopulate_state();

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
    LG_DBG("<%d>(MAP)%d: %s (%lx/%lx/%lx)", pos, map->pid, map->filename,
           map->addr, map->len, map->pgoff);
    ddprof::Dso new_dso(map->pid, map->addr, map->addr + map->len - 1,
                        map->pgoff, std::string(map->filename));
    ctx->worker_ctx.us->dso_hdr->insert_erase_overlap(std::move(new_dso));
  }
}

void ddprof_pr_lost(DDProfContext *ctx, perf_event_lost *lost, int pos) {
  (void)pos;
  ddprof_stats_add(STATS_EVENT_LOST, lost->lost, NULL);
}

void ddprof_pr_comm(DDProfContext *ctx, perf_event_comm *comm, int pos) {
  (void)ctx;
  // Change in process name (assuming exec) : clear all associated dso
  if (comm->header.misc & PERF_RECORD_MISC_COMM_EXEC) {
    LG_DBG("<%d>(COMM)%d -> %s", pos, comm->pid, comm->comm);
    ctx->worker_ctx.us->dso_hdr->pid_free(comm->pid);
  }
}

void ddprof_pr_fork(DDProfContext *ctx, perf_event_fork *frk, int pos) {
  (void)ctx;
  LG_DBG("<%d>(FORK)%d -> %d/%d", pos, frk->ppid, frk->pid, frk->tid);
  if (frk->ppid != frk->pid) {
    // Clear everything and populate at next error or with coming samples
    ctx->worker_ctx.us->dso_hdr->pid_free(frk->pid);
  }
}

void ddprof_pr_exit(DDProfContext *ctx, perf_event_exit *ext, int pos) {
  // On Linux, it seems that the thread group leader is the one whose task ID
  // matches the process ID of the group.  Moreover, it seems that it is the
  // overwhelming convention that this thread is closed after the other threads
  // (upheld by both pthreads and runtimes).
  // This could still cause problems if the ringubffer is processing samples for
  // other threads in other ringbuffers after having processed the close of the
  // thread leader locally.
  (void)ctx;
  if (ext->pid == ext->tid) {
    ctx->worker_ctx.us->dso_hdr->pid_free(ext->pid);
    LG_DBG("<%d>(EXIT)%d", pos, ext->pid);
  } else {
    LG_DBG("<%d>(EXIT)%d/%d", pos, ext->pid, ext->tid);
  }
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
  try {
    ctx->worker_ctx.exp = (DDProfExporter *)malloc(sizeof(DDProfExporter));
    if (!ctx->worker_ctx.exp) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC, "Error when creating exporter");
    }
    ctx->worker_ctx.pprof = (DDProfPProf *)malloc(sizeof(DDProfPProf));
    if (!ctx->worker_ctx.pprof) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC,
                             "Error when creating pprof structure");
    }
    DDRES_CHECK_FWD(worker_unwind_init(ctx));
    DDRES_CHECK_FWD(ddprof_exporter_init(&ctx->exp_input, ctx->worker_ctx.exp));
    // warning : depends on unwind init
    DDRES_CHECK_FWD(
        ddprof_exporter_new(ctx->worker_ctx.user_tags, ctx->worker_ctx.exp));
    DDRES_CHECK_FWD(pprof_create_profile(ctx->worker_ctx.pprof, ctx->watchers,
                                         ctx->num_watchers));
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes ddprof_worker_finish(DDProfContext *ctx) {
  try {
    DDRES_CHECK_FWD(ddprof_exporter_free(ctx->worker_ctx.exp));
    DDRES_CHECK_FWD(worker_unwind_free(ctx));
    DDRES_CHECK_FWD(pprof_free_profile(ctx->worker_ctx.pprof));
    free(ctx->worker_ctx.pprof);
    free(ctx->worker_ctx.exp);
  }
  CatchExcept2DDRes();
  return ddres_init();
}
#endif

// Simple wrapper over perf_event_hdr in order to filter by PID in a uniform
// way.  Whenver PID is a valid concept for the given event type, the interface
// uniformly presents PID as the element immediately after the header.
struct perf_event_hdr_wpid : perf_event_header {
  uint32_t pid, tid;
};

DDRes ddprof_worker(struct perf_event_header *hdr, int pos,
                    volatile bool *continue_profiling, DDProfContext *ctx) {
  // global try catch to avoid leaking exceptions to main loop
  try {
    ddprof_stats_add(STATS_EVENT_COUNT, 1, NULL);
    struct perf_event_hdr_wpid *wpid = static_cast<perf_event_hdr_wpid *>(hdr);
    switch (hdr->type) {
    /* Cases where the target type has a PID */
    case PERF_RECORD_SAMPLE:
      if (wpid->pid)
        DDRES_CHECK_FWD(ddprof_pr_sample(ctx, hdr2samp(hdr), pos));
      break;
    case PERF_RECORD_MMAP:
      if (wpid->pid)
        ddprof_pr_mmap(ctx, (perf_event_mmap *)hdr, pos);
      break;
    case PERF_RECORD_COMM:
      if (wpid->pid)
        ddprof_pr_comm(ctx, (perf_event_comm *)hdr, pos);
      break;
    case PERF_RECORD_EXIT:
      if (wpid->pid)
        ddprof_pr_exit(ctx, (perf_event_exit *)hdr, pos);
      break;
    case PERF_RECORD_FORK:
      if (wpid->pid)
        ddprof_pr_fork(ctx, (perf_event_fork *)hdr, pos);
      break;

    /* Cases where the target type might not have a PID */
    case PERF_RECORD_LOST:
      ddprof_pr_lost(ctx, (perf_event_lost *)hdr, pos);
      break;
    default:
      break;
    }

    // backpopulate if needed
    if (++ctx->worker_ctx.count_samples > s_nb_samples_per_backpopulate) {
      // allow new backpopulates and reset counter
      ctx->worker_ctx.us->dso_hdr->reset_backpopulate_state();
      ctx->worker_ctx.count_samples = 0;
    }

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
