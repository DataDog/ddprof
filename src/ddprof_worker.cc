// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_worker.hpp"

#include "ddprof_context.hpp"
#include "ddprof_perf_event.hpp"
#include "ddprof_stats.hpp"
#include "dso_hdr.hpp"
#include "dwfl_hdr.hpp"
#include "exporter/ddprof_exporter.hpp"
#include "logger.hpp"
#include "perf.hpp"
#include "pevent_lib.hpp"
#include "pprof/ddprof_pprof.hpp"
#include "procutils.hpp"
#include "stack_handler.hpp"
#include "tags.hpp"
#include "timer.hpp"
#include "unwind.hpp"
#include "unwind_helpers.hpp"
#include "unwind_state.hpp"

#include <cassert>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifdef DBG_JEMALLOC
#  include <jemalloc/jemalloc.h>
#endif

#define DDPROF_EXPORT_TIMEOUT_MAX 60

using namespace ddprof;

static const DDPROF_STATS s_cycled_stats[] = {
    STATS_UNWIND_AVG_TIME, STATS_AGGREGATION_AVG_TIME,
    STATS_EVENT_COUNT,     STATS_EVENT_LOST,
    STATS_SAMPLE_COUNT,    STATS_DSO_UNHANDLED_SECTIONS,
    STATS_TARGET_CPU_USAGE};

static const long k_clock_ticks_per_sec = sysconf(_SC_CLK_TCK);

/// Human readable runtime information
static void print_diagnostics(const DsoHdr &dso_hdr) {
  LG_NFO("Printing internal diagnostics");
  ddprof_stats_print();
  dso_hdr._stats.log();
#ifdef DBG_JEMALLOC
  // jemalloc stats
  malloc_stats_print(NULL, NULL, "");
#endif
}

static inline int64_t now_nanos() {
  static struct timeval tv = {};
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000000 + tv.tv_usec) * 1000;
}

#ifndef DDPROF_NATIVE_LIB
static DDRes report_lost_events(DDProfContext *ctx) {
  for (int watcher_idx = 0; watcher_idx < ctx->num_watchers; ++watcher_idx) {
    if (ctx->worker_ctx.lost_events_per_watcher[watcher_idx] > 0) {
      PerfWatcher *watcher = &ctx->watchers[watcher_idx];
      UnwindState *us = ctx->worker_ctx.us;
      us->output.clear();
      add_common_frame(us, SymbolErrors::lost_event);
      LG_WRN("Reporting #%lu -> [%lu] lost samples for watcher #%d",
             ctx->worker_ctx.lost_events_per_watcher[watcher_idx],
             ctx->worker_ctx.lost_events_per_watcher[watcher_idx] *
                 watcher->sample_period,
             watcher_idx);
      DDRES_CHECK_FWD(pprof_aggregate(
          &us->output, &us->symbol_hdr, watcher->sample_period,
          ctx->worker_ctx.lost_events_per_watcher[watcher_idx], watcher,
          ctx->worker_ctx.pprof[ctx->worker_ctx.i_current_pprof]));
      ctx->worker_ctx.lost_events_per_watcher[watcher_idx] = 0;
    }
  }

  return {};
}
#endif

static inline long export_time_convert(double upload_period) {
  return upload_period * 1000000000;
}

static inline void export_time_set(DDProfContext *ctx) {
  assert(ctx);
  ctx->worker_ctx.send_nanos =
      now_nanos() + export_time_convert(ctx->params.upload_period);
}

DDRes worker_library_init(DDProfContext *ctx,
                          PersistentWorkerState *persistent_worker_state) {
  try {
    // Set the initial time
    export_time_set(ctx);
    // Make sure worker-related counters are reset
    ctx->worker_ctx.count_worker = 0;
    // Make sure worker index is initialized correctly
    ctx->worker_ctx.i_current_pprof = 0;
    ctx->worker_ctx.exp_tid = {0};
    ctx->worker_ctx.us = new UnwindState(ctx->params.dd_profiling_fd);
    std::fill(ctx->worker_ctx.lost_events_per_watcher.begin(),
              ctx->worker_ctx.lost_events_per_watcher.end(), 0UL);

    // register the existing persistent storage for the state
    ctx->worker_ctx.persistent_worker_state = persistent_worker_state;

    PEventHdr *pevent_hdr = &ctx->worker_ctx.pevent_hdr;

    // If we're here, then we are a child spawned during the startup operation.
    // That means we need to iterate through the perf_event_open() handles and
    // get the mmaps
    if (!IsDDResOK(pevent_mmap(pevent_hdr, true))) {
      LG_NTC("Retrying attachment without user override");
      DDRES_CHECK_FWD(pevent_mmap(pevent_hdr, false));
    }
    // Initialize the unwind state and library
    unwind_init();
    ctx->worker_ctx.user_tags =
        new UserTags(ctx->params.tags, ctx->params.num_cpu);

    // Zero out pointers to dynamically allocated memory
    ctx->worker_ctx.exp[0] = nullptr;
    ctx->worker_ctx.exp[1] = nullptr;
    ctx->worker_ctx.pprof[0] = nullptr;
    ctx->worker_ctx.pprof[1] = nullptr;
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes worker_library_free(DDProfContext *ctx) {
  try {
    delete ctx->worker_ctx.user_tags;
    ctx->worker_ctx.user_tags = nullptr;

    PEventHdr *pevent_hdr = &ctx->worker_ctx.pevent_hdr;
    DDRES_CHECK_FWD(pevent_munmap(pevent_hdr));

    delete ctx->worker_ctx.us;
    ctx->worker_ctx.us = nullptr;
  }
  CatchExcept2DDRes();
  return ddres_init();
}

[[maybe_unused]] static DDRes worker_init_stats(DDProfWorkerContext *ctx) {
  DDRES_CHECK_FWD(proc_read(&ctx->proc_status));
  ctx->cycle_start_time = std::chrono::steady_clock::now();
  return {};
}

/// Retrieve cpu / memory info
static DDRes worker_update_stats(ProcStatus *procstat, const DsoHdr *dso_hdr,
                                 std::chrono::nanoseconds cycle_duration) {
  // Update the procstats, but first snapshot the utime so we can compute the
  // diff for the utime metric
  int64_t cpu_time_old = procstat->utime + procstat->stime;
  DDRES_CHECK_FWD(proc_read(procstat));
  int64_t elapsed_nsec = std::chrono::nanoseconds{cycle_duration}.count();
  int64_t millicores = ((procstat->utime + procstat->stime - cpu_time_old) *
                        std::nano::den * 1000) /
      (k_clock_ticks_per_sec * elapsed_nsec);
  ddprof_stats_set(STATS_PROFILER_RSS, get_page_size() * procstat->rss);
  ddprof_stats_set(STATS_PROFILER_CPU_USAGE, millicores);
  ddprof_stats_set(STATS_DSO_UNHANDLED_SECTIONS,
                   dso_hdr->_stats.sum_event_metric(DsoStats::kUnhandledDso));
  ddprof_stats_set(STATS_DSO_NEW_DSO,
                   dso_hdr->_stats.sum_event_metric(DsoStats::kNewDso));
  ddprof_stats_set(STATS_DSO_SIZE, dso_hdr->get_nb_dso());

  long target_cpu_nsec;
  ddprof_stats_get(STATS_TARGET_CPU_USAGE, &target_cpu_nsec);
  int64_t target_millicores = (target_cpu_nsec * 1000) / elapsed_nsec;
  ddprof_stats_set(STATS_TARGET_CPU_USAGE, target_millicores);

  long nsamples = 0;
  ddprof_stats_get(STATS_SAMPLE_COUNT, &nsamples);

  long tsc_cycles;
  ddprof_stats_get(STATS_UNWIND_AVG_TIME, &tsc_cycles);
  int64_t avg_unwind_ns =
      nsamples > 0 ? ddprof::tsc_cycles_to_ns(tsc_cycles) / nsamples : -1;

  ddprof_stats_set(STATS_UNWIND_AVG_TIME, avg_unwind_ns);

  ddprof_stats_get(STATS_AGGREGATION_AVG_TIME, &tsc_cycles);
  int64_t avg_aggregation_ns =
      nsamples > 0 ? ddprof::tsc_cycles_to_ns(tsc_cycles) / nsamples : -1;

  ddprof_stats_set(STATS_AGGREGATION_AVG_TIME, avg_aggregation_ns);

  if (nsamples != 0) {
    ddprof_stats_divide(STATS_UNWIND_AVG_STACK_SIZE, nsamples);
    ddprof_stats_divide(STATS_UNWIND_AVG_STACK_DEPTH, nsamples);
  } else {
    ddprof_stats_set(STATS_UNWIND_AVG_STACK_SIZE, -1);
    ddprof_stats_set(STATS_UNWIND_AVG_STACK_DEPTH, -1);
  }

  ddprof_stats_set(
      STATS_PROFILE_DURATION,
      std::chrono::duration_cast<std::chrono::milliseconds>(cycle_duration)
          .count());
  return ddres_init();
}

static DDRes ddprof_unwind_sample(DDProfContext *ctx, perf_event_sample *sample,
                                  int watcher_pos) {
  struct UnwindState *us = ctx->worker_ctx.us;
  ddprof_stats_add(STATS_SAMPLE_COUNT, 1, NULL);
  ddprof_stats_add(STATS_UNWIND_AVG_STACK_SIZE, sample->size_stack, nullptr);

  // copy the sample context into the unwind structure
  unwind_init_sample(us, sample->regs, sample->pid, sample->size_stack,
                     sample->data_stack);

  // If a sample has a PID, it has a TID.  Include it for downstream labels
  us->output.pid = sample->pid;
  us->output.tid = sample->tid;
  DDRes res = unwindstate__unwind(us);

  /* This test is not 100% accurate:
   * Linux kernel does not take into account stack start (ie. end address since
   * stack grows down) when capturing the stack, it starts from SP register and
   * only limits the range with the requested size and user space end (cf.
   * https://elixir.bootlin.com/linux/v5.19.3/source/kernel/events/core.c#L6582).
   * Then it tries to copy this range, and stops when it encounters a non-mapped
   * address
   * (https://elixir.bootlin.com/linux/v5.19.3/source/kernel/events/core.c#L6660).
   * This works well for main thread since [stack] is at the top of the process
   * user address space (and there is a gap between [vvar] and [stack]), but
   * for threads, stack can be allocated anywhere on the heap and if address
   * space below allocated stack is mapped, then kernel will happily copy the
   * whole range up to the requested sample stack size, therefore always
   * returning samples with `dyn_size` equals to the requested sample stack
   * size, even if the end of captured stack is not actually part of the stack.
   *
   * That's why we consider the stack as truncated in input only if it is also
   * detected as incomplete during unwinding.
   */
  if (sample->size_stack == ctx->watchers[watcher_pos].sample_stack_size &&
      us->output.is_incomplete) {
    ddprof_stats_add(STATS_UNWIND_TRUNCATED_INPUT, 1, nullptr);
  }

  return res;
}

/************************* perf_event_open() helpers **************************/
/// Entry point for sample aggregation
DDRes ddprof_pr_sample(DDProfContext *ctx, perf_event_sample *sample,
                       int watcher_pos) {
  if (!sample)
    return ddres_warn(DD_WHAT_PERFSAMP);

  // If this is a SW_TASK_CLOCK-type event, then aggregate the time
  if (ctx->watchers[watcher_pos].config == PERF_COUNT_SW_TASK_CLOCK)
    ddprof_stats_add(STATS_TARGET_CPU_USAGE, sample->period, NULL);

  auto ticks0 = ddprof::get_tsc_cycles();
  DDRes res = ddprof_unwind_sample(ctx, sample, watcher_pos);
  auto unwind_ticks = ddprof::get_tsc_cycles();
  ddprof_stats_add(STATS_UNWIND_AVG_TIME, unwind_ticks - ticks0, NULL);

  // Usually we want to send the sample_val, but sometimes we need to process
  // the event to get the desired value
  PerfWatcher *watcher = &ctx->watchers[watcher_pos];
  uint64_t sample_val = sample->period;
  if (PERF_SAMPLE_RAW & watcher->sample_type) {
    uint64_t raw_offset = watcher->trace_off;
    uint64_t raw_sz = watcher->trace_sz;
    memcpy(&sample_val, sample->data_raw + raw_offset, raw_sz);
  }

  // Aggregate if unwinding went well (todo : fatal error propagation)
  if (!IsDDResFatal(res)) {
    struct UnwindState *us = ctx->worker_ctx.us;

#ifndef DDPROF_NATIVE_LIB
    // in lib mode we don't aggregate (protect to avoid link failures)
    int i_export = ctx->worker_ctx.i_current_pprof;
    DDProfPProf *pprof = ctx->worker_ctx.pprof[i_export];
    DDRES_CHECK_FWD(pprof_aggregate(&us->output, &us->symbol_hdr, sample_val, 1,
                                    watcher, pprof));
    if (ctx->params.show_samples) {
      ddprof_print_sample(us->output, us->symbol_hdr, sample->period, *watcher);
    }
#else
    // Call the user's stack handler
    if (ctx->stack_handler) {
      if (!ctx->stack_handler->apply(&us->output, ctx,
                                     ctx->stack_handler->callback_ctx,
                                     watcher_pos)) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_STACK_HANDLE,
                               "Stack handler returning errors");
      }
    }
#endif
  }

  ddprof_stats_add(STATS_AGGREGATION_AVG_TIME,
                   ddprof::get_tsc_cycles() - unwind_ticks, NULL);

  return {};
}

DDRes ddprof_pr_allocation_tracking(DDProfContext *ctx,
                                    perf_event_sample *sample,
                                    int watcher_pos) {
  if (!sample)
    return ddres_warn(DD_WHAT_PERFSAMP);

  // If this is a SW_TASK_CLOCK-type event, then aggregate the time
  if (ctx->watchers[watcher_pos].config == PERF_COUNT_SW_TASK_CLOCK)
    ddprof_stats_add(STATS_TARGET_CPU_USAGE, sample->period, NULL);

  auto ticks0 = ddprof::get_tsc_cycles();
  DDRes res = ddprof_unwind_sample(ctx, sample, watcher_pos);
  auto unwind_ticks = ddprof::get_tsc_cycles();
  ddprof_stats_add(STATS_UNWIND_AVG_TIME, unwind_ticks - ticks0, NULL);

  // Aggregate if unwinding went well (todo : fatal error propagation)
  if (!IsDDResFatal(res)) {
    struct UnwindState *us = ctx->worker_ctx.us;
    ctx->worker_ctx.live_allocation.register_allocation(
        us->output, sample->addr, sample->period, watcher_pos, sample->pid);
  }

  // TODO: propagate fatal
  return ddres_init();
}

DDRes ddprof_pr_sysallocation_tracking(DDProfContext *ctx,
                                       perf_event_sample *sample,
                                       int watcher_pos) {
  int64_t id;
  memcpy(&id, sample->data_raw + 8, sizeof(id));
  auto &sysalloc = ctx->worker_ctx.sys_allocation;

  // Only unwind if we will need to propagate unwinding information forward
  DDRes res = {};
  UnwindOutput *uwo = NULL;
  if (id == 9 || id == 25) {
    auto ticks0 = ddprof::get_tsc_cycles();
    res = ddprof_unwind_sample(ctx, sample, watcher_pos);
    auto unwind_ticks = ddprof::get_tsc_cycles();
    ddprof_stats_add(STATS_UNWIND_AVG_TIME, unwind_ticks - ticks0, NULL);
    uwo = &ctx->worker_ctx.us->output;

    // TODO: propagate fatal
    if (IsDDResFatal(res)) {
      return ddres_init();
    }
  }

  // Syscall parameters.  Suppressing nags because it's annoying to look these
  // up and it isn't totally appropriate to spin out a new header just
  // for this
#ifdef __x86_64__
  [[maybe_unused]] uint64_t sc_ret = sample->regs[PAM_X86_RAX];
  [[maybe_unused]] uint64_t sc_p1 = sample->regs[PAM_X86_RDI];
  [[maybe_unused]] uint64_t sc_p2 = sample->regs[PAM_X86_RSI];
  [[maybe_unused]] uint64_t sc_p3 = sample->regs[PAM_X86_RDX];
  [[maybe_unused]] uint64_t sc_p4 = sample->regs[PAM_X86_R10];
  [[maybe_unused]] uint64_t sc_p5 = sample->regs[PAM_X86_R8];
  [[maybe_unused]] uint64_t sc_p6 = sample->regs[PAM_X86_R9];
#elif __aarch64__
  // Obviously ARM is totally broken here.
  [[maybe_unused]] uint64_t sc_ret = sample->regs[PAM_ARM_X0];
  [[maybe_unused]] uint64_t sc_p1 = sample->regs[PAM_ARM_X0];
  [[maybe_unused]] uint64_t sc_p2 = sample->regs[PAM_ARM_X1];
  [[maybe_unused]] uint64_t sc_p3 = sample->regs[PAM_ARM_X2];
  [[maybe_unused]] uint64_t sc_p4 = sample->regs[PAM_ARM_X3];
  [[maybe_unused]] uint64_t sc_p5 = sample->regs[PAM_ARM_X4];
  [[maybe_unused]] uint64_t sc_p6 = sample->regs[PAM_ARM_X5];
#else
#  error Architecture not supported
#endif
  if (sc_ret > -4096UL) {
    // If the syscall returned error, it didn't mutate state.  Skip!
    // ("high" values are errors, as per standard)
    return ddres_init();
  }

  // hardcoded syscall numbers; these are uniform between x86/arm
  PRINT_NFO("SYSCALL: %lu", id);
  if (id == 9) {
    sysalloc.do_mmap(*uwo, sc_ret, sc_p2, sample->pid);
  } else if (id == 11) {
    sysalloc.do_munmap(sc_p1, sc_p2, sample->pid);
  } else if (id == 28) {
    // Unhandled, no need to handle
  } else if (id == 25) {
    sysalloc.do_mremap(*uwo, sc_ret, sc_p1, sc_p2, sc_p3, sample->pid);
  } else if (id == 60 || id == 231 || id == 59 || id == 322 || id == 520 ||
             id == 545) {
    // Erase upon exit or exec
    sysalloc.do_exit(sample->pid);
  }

  return ddres_init();
}

static void ddprof_reset_worker_stats() {
  for (unsigned i = 0; i < std::size(s_cycled_stats); ++i) {
    ddprof_stats_clear(s_cycled_stats[i]);
  }
}

#ifndef DDPROF_NATIVE_LIB
void *ddprof_worker_export_thread(void *arg) {
  DDProfWorkerContext *worker = (DDProfWorkerContext *)arg;
  // export the one we are not writting to
  int i = 1 - worker->i_current_pprof;
  // Increase number of sequences in persistent storage
  // This should start at 0, and if exporter thread
  // gets joined forcefully, we should not resume on same value
  uint32_t profile_seq = (worker->persistent_worker_state->profile_seq)++;

  if (IsDDResFatal(ddprof_exporter_export(worker->pprof[i]->_profile,
                                          worker->pprof[i]->_tags, profile_seq,
                                          worker->exp[i]))) {
    LG_NFO("Failed to export from worker");
    worker->exp_error = true;
  }

  return nullptr;
}
#endif

#ifndef DDPROF_NATIVE_LIB
static DDRes aggregate_stack(const LiveAllocation::AllocationInfo &alloc_info,
                             DDProfContext *ctx) {
  struct UnwindState *us = ctx->worker_ctx.us;
  int watcher_pos = alloc_info._watcher_pos;
  PerfWatcher *watcher = &ctx->watchers[watcher_pos];
  int i_export = ctx->worker_ctx.i_current_pprof;
  DDProfPProf *pprof = ctx->worker_ctx.pprof[i_export];
  DDRES_CHECK_FWD(pprof_aggregate(&alloc_info._stack, &us->symbol_hdr,
                                  alloc_info._size, 1, watcher, pprof));
  if (ctx->params.show_samples) {
    ddprof_print_sample(alloc_info._stack, us->symbol_hdr, alloc_info._size,
                        *watcher);
  }
  return ddres_init();
}

static DDRes aggregate_live_allocations(DDProfContext *ctx) {
  // this would be more efficient if we could reuse the same stacks in
  // libdatadog
  LiveAllocation &live_allocations = ctx->worker_ctx.live_allocation;
  for (auto &stack_map : live_allocations._pid_map) {
    for (const auto &alloc_info_pair : stack_map.second) {
      DDRES_CHECK_FWD(aggregate_stack(alloc_info_pair.second, ctx));
    }
    LG_NTC("Number of Live allocations for PID%d = %lu ", stack_map.first,
           stack_map.second.size());
    // Safety to avoid spending all the time reporting allocations
    if (stack_map.second.size() >= LiveAllocation::kMaxTracked) {
      stack_map.second.clear();
    }
  }
  return ddres_init();
}

static DDRes aggregate_sys_allocations(DDProfContext *ctx) {
  struct UnwindState *us = ctx->worker_ctx.us;
  SystemAllocation &sysallocs = ctx->worker_ctx.sys_allocation;
  PerfWatcher *watcher = &ctx->watchers[sysallocs.watcher_pos];
  int i_export = ctx->worker_ctx.i_current_pprof;
  DDProfPProf *pprof = ctx->worker_ctx.pprof[i_export];

  // Iterate through each PID
  for (auto &stack_map : sysallocs._pid_map) {

    // Iterate through pages...
    // TODO Probably aggregate into ranges of pages or something, but once per
    //      page is just too much
    for (const auto &page : stack_map.second) {
      DDRES_CHECK_FWD(pprof_aggregate(&page.second, &us->symbol_hdr, 4096, 1,
                                      watcher, pprof));
    }
  }
  return ddres_init();
}
#endif

/// Cycle operations : export, sync metrics, update counters
DDRes ddprof_worker_cycle(DDProfContext *ctx, int64_t now,
                          [[maybe_unused]] bool synchronous_export) {

#ifndef DDPROF_NATIVE_LIB
  // TODO: lib mode (unhandled for now)
  DDRES_CHECK_FWD(aggregate_live_allocations(ctx));
  DDRES_CHECK_FWD(aggregate_sys_allocations(ctx));

  // Take the current pprof contents and ship them to the backend.  This also
  // clears the pprof for reuse
  // Dispatch happens in a thread, with the underlying data structure for
  // aggregation rotating between exports.  If we return to this point before
  // the previous thread has finished,  we wait for up to five seconds before
  // failing

  // If something is pending, return error
  if (ctx->worker_ctx.exp_tid) {
    struct timespec waittime;
    clock_gettime(CLOCK_REALTIME, &waittime);
    int waitsec = DDPROF_EXPORT_TIMEOUT_MAX - ctx->params.upload_period;
    waitsec = waitsec > 1 ? waitsec : 1;
    waittime.tv_sec += waitsec;
    if (pthread_timedjoin_np(ctx->worker_ctx.exp_tid, NULL, &waittime)) {
      LG_WRN("Exporter took too long");
      return ddres_create(DD_SEVERROR, DD_WHAT_EXPORT_TIMEOUT);
    }
    ctx->worker_ctx.exp_tid = 0;
  }
  if (ctx->worker_ctx.exp_error) {
    return ddres_create(DD_SEVERROR, DD_WHAT_EXPORTER);
  }

  DDRES_CHECK_FWD(report_lost_events(ctx));

  // Dispatch to thread
  ctx->worker_ctx.exp_error = false;

  // switch before we async export to avoid any possible race conditions (then
  // take into account the switch)
  ctx->worker_ctx.i_current_pprof = 1 - ctx->worker_ctx.i_current_pprof;

  // Reset the current, ensuring the timestamp starts when we are about to write
  // to it
  DDRES_CHECK_FWD(
      pprof_reset(ctx->worker_ctx.pprof[ctx->worker_ctx.i_current_pprof]));

  if (!synchronous_export) {
    pthread_create(&ctx->worker_ctx.exp_tid, NULL, ddprof_worker_export_thread,
                   &ctx->worker_ctx);
  } else {
    ddprof_worker_export_thread(reinterpret_cast<void *>(&ctx->worker_ctx));
    if (ctx->worker_ctx.exp_error) {
      return ddres_create(DD_SEVERROR, DD_WHAT_EXPORTER);
    }
  }
#endif
  auto cycle_now = std::chrono::steady_clock::now();
  auto cycle_duration = cycle_now - ctx->worker_ctx.cycle_start_time;
  ctx->worker_ctx.cycle_start_time = cycle_now;

  // Scrape procfs for process usage statistics
  DDRES_CHECK_FWD(worker_update_stats(&ctx->worker_ctx.proc_status,
                                      &ctx->worker_ctx.us->dso_hdr,
                                      cycle_duration));

  // And emit diagnostic output (if it's enabled)
  print_diagnostics(ctx->worker_ctx.us->dso_hdr);
  if (IsDDResNotOK(ddprof_stats_send(ctx->params.internal_stats))) {
    LG_WRN("Unable to utilize to statsd socket.  Suppressing future stats.");
    free((void *)ctx->params.internal_stats);
    ctx->params.internal_stats = NULL;
  }

  // Increase the counts of exports
  ctx->worker_ctx.count_worker += 1;

  // allow new backpopulates
  ctx->worker_ctx.us->dso_hdr.reset_backpopulate_state();

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
  unwind_cycle(ctx->worker_ctx.us);

  // Reset stats relevant to a single cycle
  ddprof_reset_worker_stats();

  return ddres_init();
}

void ddprof_pr_mmap(DDProfContext *ctx, const perf_event_mmap2 *map,
                    int watcher_pos) {
  LG_DBG("<%d>(MAP)%d: %s (%lx/%lx/%lx) %02u:%02u %lu", watcher_pos, map->pid,
         map->filename, map->addr, map->len, map->pgoff, map->maj, map->min,
         map->ino);
  ddprof::Dso new_dso(map->pid, map->addr, map->addr + map->len - 1, map->pgoff,
                      std::string(map->filename), true, map->ino);
  ctx->worker_ctx.us->dso_hdr.insert_erase_overlap(std::move(new_dso));
}

void ddprof_pr_lost(DDProfContext *ctx, const perf_event_lost *lost,
                    int watcher_pos) {
  ddprof_stats_add(STATS_EVENT_LOST, lost->lost, NULL);
  ctx->worker_ctx.lost_events_per_watcher[watcher_pos] += lost->lost;
}

void ddprof_pr_comm(DDProfContext *ctx, const perf_event_comm *comm,
                    int watcher_pos) {
  // Change in process name (assuming exec) : clear all associated dso
  if (comm->header.misc & PERF_RECORD_MISC_COMM_EXEC) {
    LG_DBG("<%d>(COMM)%d -> %s", watcher_pos, comm->pid, comm->comm);
    unwind_pid_free(ctx->worker_ctx.us, comm->pid);
  }
}

void ddprof_pr_fork(DDProfContext *ctx, const perf_event_fork *frk,
                    int watcher_pos) {
  LG_DBG("<%d>(FORK)%d -> %d/%d", watcher_pos, frk->ppid, frk->pid, frk->tid);
  if (frk->ppid != frk->pid) {
    // Clear everything and populate at next error or with coming samples
    unwind_pid_free(ctx->worker_ctx.us, frk->pid);
  }
}

void ddprof_pr_exit(DDProfContext *ctx, const perf_event_exit *ext,
                    int watcher_pos) {
  // On Linux, it seems that the thread group leader is the one whose task ID
  // matches the process ID of the group.  Moreover, it seems that it is the
  // overwhelming convention that this thread is closed after the other threads
  // (upheld by both pthreads and runtimes).
  // We do not clear the PID at this time because we currently cleanup anyway.
  (void)ctx;
  if (ext->pid == ext->tid) {
    LG_DBG("<%d>(EXIT)%d", watcher_pos, ext->pid);
  } else {
    LG_DBG("<%d>(EXIT)%d/%d", watcher_pos, ext->pid, ext->tid);
  }
}

void ddprof_pr_deallocation(DDProfContext *ctx,
                            const DeallocationEvent *event) {
  ctx->worker_ctx.live_allocation.register_deallocation(event->ptr,
                                                        event->sample_id.pid);
}

/********************************** callbacks *********************************/
DDRes ddprof_worker_maybe_export(DDProfContext *ctx, int64_t now_ns) {
  try {
    if (now_ns > ctx->worker_ctx.send_nanos) {
      // restart worker if number of uploads is reached
      ctx->worker_ctx.persistent_worker_state->restart_worker =
          (ctx->worker_ctx.count_worker + 1 >= ctx->params.worker_period);
      // when restarting worker, do a synchronous export
      DDRES_CHECK_FWD(ddprof_worker_cycle(
          ctx, now_ns,
          ctx->worker_ctx.persistent_worker_state->restart_worker));
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}

#ifndef DDPROF_NATIVE_LIB
DDRes ddprof_worker_init(DDProfContext *ctx,
                         PersistentWorkerState *persistent_worker_state) {
  try {
    DDRES_CHECK_FWD(worker_library_init(ctx, persistent_worker_state));
    ctx->worker_ctx.exp[0] =
        (DDProfExporter *)calloc(1, sizeof(DDProfExporter));
    ctx->worker_ctx.exp[1] =
        (DDProfExporter *)calloc(1, sizeof(DDProfExporter));
    ctx->worker_ctx.pprof[0] = new DDProfPProf();
    ctx->worker_ctx.pprof[1] = new DDProfPProf();
    if (!ctx->worker_ctx.exp[0] || !ctx->worker_ctx.exp[1]) {
      free(ctx->worker_ctx.exp[0]);
      free(ctx->worker_ctx.exp[1]);
      delete ctx->worker_ctx.pprof[0];
      delete ctx->worker_ctx.pprof[1];
      DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC, "Error creating exporter");
    }

    DDRES_CHECK_FWD(
        ddprof_exporter_init(&ctx->exp_input, ctx->worker_ctx.exp[0]));
    DDRES_CHECK_FWD(
        ddprof_exporter_init(&ctx->exp_input, ctx->worker_ctx.exp[1]));
    // warning : depends on unwind init
    DDRES_CHECK_FWD(
        ddprof_exporter_new(ctx->worker_ctx.user_tags, ctx->worker_ctx.exp[0]));
    DDRES_CHECK_FWD(
        ddprof_exporter_new(ctx->worker_ctx.user_tags, ctx->worker_ctx.exp[1]));

    DDRES_CHECK_FWD(pprof_create_profile(ctx->worker_ctx.pprof[0], ctx));
    DDRES_CHECK_FWD(pprof_create_profile(ctx->worker_ctx.pprof[1], ctx));
    DDRES_CHECK_FWD(worker_init_stats(&ctx->worker_ctx));
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes ddprof_worker_free(DDProfContext *ctx) {
  try {
    // First, see if there are any outstanding requests and give them a token
    // amount of time to complete
    if (ctx->worker_ctx.exp_tid) {
      struct timespec waittime;
      clock_gettime(CLOCK_REALTIME, &waittime);
      waittime.tv_sec += 5;
      if (pthread_timedjoin_np(ctx->worker_ctx.exp_tid, NULL, &waittime)) {
        pthread_cancel(ctx->worker_ctx.exp_tid);
      }
      ctx->worker_ctx.exp_tid = 0;
    }

    DDRES_CHECK_FWD(worker_library_free(ctx));
    for (int i = 0; i < 2; i++) {
      if (ctx->worker_ctx.exp[i]) {
        DDRES_CHECK_FWD(ddprof_exporter_free(ctx->worker_ctx.exp[i]));
        free(ctx->worker_ctx.exp[i]);
        ctx->worker_ctx.exp[i] = nullptr;
      }
      if (ctx->worker_ctx.pprof[i]) {
        DDRES_CHECK_FWD(pprof_free_profile(ctx->worker_ctx.pprof[i]));
        delete ctx->worker_ctx.pprof[i];
        ctx->worker_ctx.pprof[i] = nullptr;
      }
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}
#endif

// Simple wrapper over perf_event_hdr in order to filter by PID in a uniform
// way.  Whenver PID is a valid concept for the given event type, the
// interface uniformly presents PID as the element immediately after the
// header.
struct perf_event_hdr_wpid : perf_event_header {
  uint32_t pid, tid;
};

DDRes ddprof_worker_process_event(const perf_event_header *hdr, int watcher_pos,
                                  DDProfContext *ctx) {
  // global try catch to avoid leaking exceptions to main loop
  try {
    ddprof_stats_add(STATS_EVENT_COUNT, 1, NULL);
    const perf_event_hdr_wpid *wpid =
        static_cast<const perf_event_hdr_wpid *>(hdr);
    PerfWatcher *watcher = &ctx->watchers[watcher_pos];
    switch (hdr->type) {
    /* Cases where the target type has a PID */
    case PERF_RECORD_SAMPLE:
      if (wpid->pid) {
        uint64_t mask = watcher->sample_type;
        perf_event_sample *sample = hdr2samp(hdr, mask);

        // Various checks for allocation profiling
        // - sALLOC
        // - mmap/munmap syscalls
        bool is_allocation = watcher->type == kDDPROF_TYPE_CUSTOM &&
            watcher->config == kDDPROF_COUNT_ALLOCATIONS;
        if (sample) {

          // Handle special profiling types first
          if (watcher->ddprof_event_type == DDPROF_PWE_tALLOCSYS) {
            DDRES_CHECK_FWD(
                ddprof_pr_sysallocation_tracking(ctx, sample, watcher_pos));
          } else if (is_allocation && ctx->params.live_allocations) {
            DDRES_CHECK_FWD(
                ddprof_pr_allocation_tracking(ctx, sample, watcher_pos));
          } else {
            DDRES_CHECK_FWD(ddprof_pr_sample(ctx, sample, watcher_pos));
          }
        }
      }
      break;
    case PERF_RECORD_MMAP2:
      if (wpid->pid)
        ddprof_pr_mmap(ctx, reinterpret_cast<const perf_event_mmap2 *>(hdr),
                       watcher_pos);
      break;
    case PERF_RECORD_COMM:
      if (wpid->pid)
        ddprof_pr_comm(ctx, reinterpret_cast<const perf_event_comm *>(hdr),
                       watcher_pos);
      break;
    case PERF_RECORD_EXIT:
      if (wpid->pid)
        ddprof_pr_exit(ctx, reinterpret_cast<const perf_event_exit *>(hdr),
                       watcher_pos);
      break;
    case PERF_RECORD_FORK:
      if (wpid->pid)
        ddprof_pr_fork(ctx, reinterpret_cast<const perf_event_fork *>(hdr),
                       watcher_pos);
      break;

    /* Cases where the target type might not have a PID */
    case PERF_RECORD_LOST:
      ddprof_pr_lost(ctx, reinterpret_cast<const perf_event_lost *>(hdr),
                     watcher_pos);
      break;
    case PERF_CUSTOM_EVENT_DEALLOCATION:
      ddprof_pr_deallocation(ctx,
                             reinterpret_cast<const DeallocationEvent *>(hdr));
    default:
      break;
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}
