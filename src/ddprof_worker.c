#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <x86intrin.h>

#include "ddprof/pprof.h"
#include "ddprof_context.h"
#include "ddprof_export.h"
#include "ddprof_stats.h"
#include "unwind.h"

#include "ddprof_worker.h"

static inline int64_t now_nanos() {
  static struct timeval tv = {0};
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000000 + tv.tv_usec) * 1000;
}

/************************* perf_event_open() helpers **************************/
/// Entry point for sample aggregation
void ddprof_pr_sample(DDProfContext *ctx, struct perf_event_sample *sample,
                      int pos) {
  // Before we do anything else, copy the perf_event_header into a sample
  static uint64_t id_locs[MAX_STACK] = {0};
  struct UnwindState *us = ctx->us;
  DProf *dp = ctx->dp;
  ddprof_stats_add(STATS_SAMPLE_COUNT, 1);
  us->pid = sample->pid;
  us->idx = 0; // Modified during unwinding; has stack depth
  us->stack = NULL;
  us->stack_sz = sample->size_stack;
  us->stack = sample->data_stack;
  memcpy(&us->regs[0], sample->regs, 3 * sizeof(uint64_t));
  us->max_stack = MAX_STACK;
  FunLoc_clear(us->locs);
  unsigned long this_ticks_unwind = __rdtsc();
  if (IsDDResNotOK(unwindstate__unwind(us))) {
    Dso *dso = dso_find(us->pid, us->eip);
    if (!dso) {
      LG_WRN("Error getting map for [%d](0x%lx)", us->pid, us->eip);
      analyze_unwinding_error(us->pid, us->eip);
    } else {
      LG_WRN("Error unwinding %s [%d](0x%lx)", dso_path(dso), us->pid, us->eip);
    }
    return;
  }
  ddprof_stats_add(STATS_UNWIND_TICKS, __rdtsc() - this_ticks_unwind);
  FunLoc *locs = us->locs;
  for (uint64_t i = 0, j = 0; i < us->idx; i++) {
    FunLoc L = locs[i];
    uint64_t id_map, id_fun, id_loc;

    // Using the sopath instead of srcpath in locAdd for the DD UI
    id_map = pprof_mapAdd(dp, L.map_start, L.map_end, L.map_off, L.sopath, "");
    id_fun = pprof_funAdd(dp, L.funname, L.funname, L.srcpath, 0);
    id_loc = pprof_locAdd(dp, id_map, 0, (uint64_t[]){id_fun},
                          (int64_t[]){L.line}, 1);
    if (id_loc > 0)
      id_locs[j++] = id_loc;
  }
  int64_t sample_val[MAX_TYPE_WATCHER] = {0};
  sample_val[pos] = sample->period;
  pprof_sampleAdd(dp, sample_val, ctx->num_watchers, id_locs, us->idx);
}

void ddprof_pr_mmap(DDProfContext *ctx, perf_event_mmap *map, int pos) {
  (void)ctx;
  if (!(map->header.misc & PERF_RECORD_MISC_MMAP_DATA)) {
    LG_DBG("[PERF]<%d>(MAP)%d: %s (%lx/%lx/%lx)", pos, map->pid, map->filename,
           map->addr, map->len, map->pgoff);
    DsoIn in = *(DsoIn *)&map->addr;
    in.filename = map->filename;
    pid_add(map->pid, &in);
  }
}

void ddprof_pr_lost(DDProfContext *ctx, perf_event_lost *lost, int pos) {
  (void)ctx;
  (void)pos;
  ddprof_stats_add(STATS_EVENT_LOST, lost->lost);
}

void ddprof_pr_comm(DDProfContext *ctx, perf_event_comm *comm, int pos) {
  (void)ctx;
  if (comm->header.misc & PERF_RECORD_MISC_COMM_EXEC) {
    LG_DBG("[PERF]<%d>(COMM)%d", pos, comm->pid);
    pid_free(comm->pid);
  }
}

void ddprof_pr_fork(DDProfContext *ctx, perf_event_fork *frk, int pos) {
  (void)ctx;
  LG_DBG("[PERF]<%d>(FORK)%d -> %d", pos, frk->ppid, frk->pid);
  if (frk->ppid != frk->pid) {
    pid_fork(frk->ppid, frk->pid);
  } else {
    pid_free(frk->pid);
    pid_backpopulate(frk->pid);
  }
}

void ddprof_pr_exit(DDProfContext *ctx, perf_event_exit *ext, int pos) {
  (void)ctx;
  LG_DBG("[PERF]<%d>(EXIT)%d", pos, ext->pid);
  pid_free(ext->pid);
}

/****************************** other functions *******************************/
DDRes reset_state(DDProfContext *ctx, volatile bool *continue_profiling) {
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
  if (ctx->params.worker_period <= ctx->count_worker) {
    *continue_profiling = true;
    DDRES_RETURN_WARN_LOG(DD_WHAT_WORKER_RESET, "%s: cnt=%u - stop worker (%s)",
                          __FUNCTION__, ctx->count_worker,
                          (*continue_profiling) ? "continue" : "stop");
  }

  // If we haven't hit the hard cap, have we hit the soft cap?
  if (ctx->params.cache_period <= ctx->count_cache) {
    ctx->count_cache = 0;
    DDRES_CHECK_FWD(dwfl_caches_clear(ctx->us));

    // Clear and re-initialize the pprof
    const char *pprof_labels[MAX_TYPE_WATCHER];
    const char *pprof_units[MAX_TYPE_WATCHER];
    pprof_Free(ctx->dp);
    for (int i = 0; i < ctx->num_watchers; i++) {
      pprof_labels[i] = ctx->watchers[i].label;
      pprof_units[i] = ctx->watchers[i].unit;
    }

    if (!pprof_Init(ctx->dp, (const char **)pprof_labels,
                    (const char **)pprof_units, ctx->num_watchers)) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_UKNW,
                             "[DDPROF] Error refreshing profile storage");
    }
  }

  return ddres_init();
}

DDRes ddprof_timeout(volatile bool *continue_profiling, void *arg) {
  DDProfContext *ctx = arg;
  int64_t now = now_nanos();
  if (now > ctx->send_nanos) {
    DDRES_CHECK_FWD(export(ctx, now));
    // reset state defines if we should reboot the worker
    return reset_state(ctx, continue_profiling);
  }
  return ddres_init();
}

DDRes ddprof_worker_init(void *arg) {
  DDProfContext *ctx = arg;

  // Set the initial time
  ctx->send_nanos = now_nanos() + ctx->params.upload_period * 1000000000;

  // Initialize the unwind state and library
  DDRES_CHECK_FWD(unwind_init(ctx->us));
  return ddres_init();
}

DDRes ddprof_worker_finish(void *arg, bool is_final) {
  DDProfContext *ctx = arg;

  // We're going to close down, but first check whether we have a valid export
  // to send (or if we requested the last partial export with sendfinal)
  if (is_final) {
    int64_t now = now_nanos();
    if (now > ctx->send_nanos || ctx->sendfinal) {
      LG_WRN("Sending final export");
      if (IsDDResNotOK(export(ctx, now))) {
        LG_ERR("Error when exporting.");
      }
    }
  }
  return ddres_init();
}

DDRes ddprof_worker(struct perf_event_header *hdr, int pos,
                    volatile bool *continue_profiling, void *arg) {
  DDProfContext *ctx = arg;

  switch (hdr->type) {
  case PERF_RECORD_SAMPLE:
    ddprof_pr_sample(ctx, hdr2samp(hdr), pos);
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

  // Click the timer at the end of processing, since we always add the sampling
  // rate to the last time.
  int64_t now = now_nanos();

  if (now > ctx->send_nanos) {
    DDRES_CHECK_FWD(export(ctx, now));
    // reset state defines if we should reboot the worker
    DDRes res = reset_state(ctx, continue_profiling);
    // A warning can be returned for a reset and should not be ignored
    if (IsDDResNotOK(res)) {
      return res;
    }
  }
  return ddres_init();
}
