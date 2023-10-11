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
#include "tags.hpp"
#include "timer.hpp"
#include "unwind.hpp"
#include "unwind_helpers.hpp"
#include "unwind_state.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <sys/time.h>
#include <unistd.h>

static constexpr std::chrono::seconds k_export_timeout{60};

namespace ddprof {

namespace {

const DDPROF_STATS s_cycled_stats[] = {
    STATS_UNWIND_AVG_TIME, STATS_AGGREGATION_AVG_TIME,
    STATS_EVENT_COUNT,     STATS_EVENT_LOST,
    STATS_SAMPLE_COUNT,    STATS_DSO_UNHANDLED_SECTIONS,
    STATS_TARGET_CPU_USAGE};

const long k_clock_ticks_per_sec = sysconf(_SC_CLK_TCK);

/// Remove all structures related to
DDRes worker_pid_free(DDProfContext &ctx, pid_t el);

DDRes clear_unvisited_pids(DDProfContext &ctx);

/// Human readable runtime information
void print_diagnostics(const DsoHdr &dso_hdr) {
  LG_NFO("Printing internal diagnostics");
  ddprof_stats_print();
  dso_hdr._stats.log();
}

DDRes report_lost_events(DDProfContext &ctx) {
  for (unsigned watcher_idx = 0; watcher_idx < ctx.watchers.size();
       ++watcher_idx) {
    auto nb_lost = ctx.worker_ctx.lost_events_per_watcher[watcher_idx];

    if (nb_lost > 0) {
      PerfWatcher *watcher = &ctx.watchers[watcher_idx];
      UnwindState *us = ctx.worker_ctx.us;
      us->output.clear();
      add_common_frame(us, SymbolErrors::lost_event);

      auto period = (watcher->options.is_freq)
          ? std::chrono::nanoseconds(std::chrono::seconds{1}).count() /
              watcher->sample_frequency
          : watcher->sample_period;

      auto value = period * nb_lost;
      LG_WRN("Reporting %lu lost samples (cumulated lost value: %lu) for "
             "watcher #%d",
             nb_lost, value, watcher_idx);
      DDRES_CHECK_FWD(pprof_aggregate(
          &us->output, us->symbol_hdr, value, nb_lost, watcher, kSumPos,
          ctx.worker_ctx.pprof[ctx.worker_ctx.i_current_pprof]));
      ctx.worker_ctx.lost_events_per_watcher[watcher_idx] = 0;
    }
  }

  return {};
}

inline void export_time_set(DDProfContext &ctx) {
  ctx.worker_ctx.send_time =
      std::chrono::steady_clock::now() + ctx.params.upload_period;
}

DDRes symbols_update_stats(const SymbolHdr &symbol_hdr) {
  const auto &stats = symbol_hdr._runtime_symbol_lookup.get_stats();
  DDRES_CHECK_FWD(
      ddprof_stats_set(STATS_SYMBOLS_JIT_READS, stats._nb_jit_reads));
  DDRES_CHECK_FWD(ddprof_stats_set(STATS_SYMBOLS_JIT_FAILED_LOOKUPS,
                                   stats._nb_failed_lookups));
  DDRES_CHECK_FWD(
      ddprof_stats_set(STATS_SYMBOLS_JIT_SYMBOL_COUNT, stats._symbol_count));
  return ddres_init();
}

/// Retrieve cpu / memory info
DDRes worker_update_stats(ProcStatus *procstat, const UnwindState &us,
                          std::chrono::nanoseconds cycle_duration) {
  const DsoHdr &dso_hdr = us.dso_hdr;
  // Update the procstats, but first snapshot the utime so we can compute the
  // diff for the utime metric
  int64_t const cpu_time_old = procstat->utime + procstat->stime;
  DDRES_CHECK_FWD(proc_read(procstat));
  int64_t const elapsed_nsec = std::chrono::nanoseconds{cycle_duration}.count();
  int64_t const millicores =
      ((procstat->utime + procstat->stime - cpu_time_old) * std::nano::den *
       1000) / // NOLINT(readability-magic-numbers)
      (k_clock_ticks_per_sec * elapsed_nsec);
  ddprof_stats_set(STATS_PROFILER_RSS, get_page_size() * procstat->rss);
  ddprof_stats_set(STATS_PROFILER_CPU_USAGE, millicores);
  ddprof_stats_set(STATS_DSO_UNHANDLED_SECTIONS,
                   dso_hdr._stats.sum_event_metric(DsoStats::kUnhandledDso));
  ddprof_stats_set(STATS_DSO_NEW_DSO,
                   dso_hdr._stats.sum_event_metric(DsoStats::kNewDso));
  ddprof_stats_set(STATS_DSO_SIZE, dso_hdr.get_nb_dso());

  // Symbol stats
  DDRES_CHECK_FWD(symbols_update_stats(us.symbol_hdr));

  long target_cpu_nsec;
  ddprof_stats_get(STATS_TARGET_CPU_USAGE, &target_cpu_nsec);
  // NOLINTNEXTLINE(readability-magic-numbers)
  int64_t const target_millicores = (target_cpu_nsec * 1000) / elapsed_nsec;
  ddprof_stats_set(STATS_TARGET_CPU_USAGE, target_millicores);

  long nsamples = 0;
  ddprof_stats_get(STATS_SAMPLE_COUNT, &nsamples);

  long tsc_cycles;
  ddprof_stats_get(STATS_UNWIND_AVG_TIME, &tsc_cycles);
  int64_t const avg_unwind_ns =
      nsamples > 0 ? tsc_cycles_to_ns(tsc_cycles) / nsamples : -1;

  ddprof_stats_set(STATS_UNWIND_AVG_TIME, avg_unwind_ns);

  ddprof_stats_get(STATS_AGGREGATION_AVG_TIME, &tsc_cycles);
  int64_t const avg_aggregation_ns =
      nsamples > 0 ? tsc_cycles_to_ns(tsc_cycles) / nsamples : -1;

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

DDRes ddprof_unwind_sample(DDProfContext &ctx, perf_event_sample *sample,
                           int watcher_pos) {
  struct UnwindState *us = ctx.worker_ctx.us;
  PerfWatcher *watcher = &ctx.watchers[watcher_pos];

  ddprof_stats_add(STATS_SAMPLE_COUNT, 1, nullptr);
  ddprof_stats_add(STATS_UNWIND_AVG_STACK_SIZE, sample->size_stack, nullptr);

  // copy the sample context into the unwind structure
  unwind_init_sample(us, sample->regs, sample->pid, sample->size_stack,
                     sample->data_stack);

  // If a sample has a PID, it has a TID.  Include it for downstream labels
  us->output.pid = sample->pid;
  us->output.tid = sample->tid;

  // If this is a SW_TASK_CLOCK-type event, then aggregate the time
  if (watcher->config == PERF_COUNT_SW_TASK_CLOCK) {
    ddprof_stats_add(STATS_TARGET_CPU_USAGE, sample->period, nullptr);
  }

  // Attempt to fully unwind if the watcher has a callgraph type
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
  if (sample->size_stack ==
          ctx.watchers[watcher_pos].options.stack_sample_size &&
      us->output.is_incomplete) {
    ddprof_stats_add(STATS_UNWIND_TRUNCATED_INPUT, 1, nullptr);
  }

  if (us->_dwfl_wrapper->_inconsistent) {
    // Loaded modules were inconsistent, assume we should flush everything.
    LG_WRN("(Inconsistent DWFL/DSOs)%d - Free associated objects", us->pid);
    DDRES_CHECK_FWD(worker_pid_free(ctx, us->pid));
  }
  return res;
}

void ddprof_reset_worker_stats() {
  for (auto s_cycled_stat : s_cycled_stats) {
    ddprof_stats_clear(s_cycled_stat);
  }
}

DDRes aggregate_livealloc_stack(
    const LiveAllocation::PprofStacks::value_type &alloc_info,
    DDProfContext &ctx, const PerfWatcher *watcher, DDProfPProf *pprof,
    const SymbolHdr &symbol_hdr) {
  DDRES_CHECK_FWD(
      pprof_aggregate(&alloc_info.first, symbol_hdr, alloc_info.second._value,
                      alloc_info.second._count, watcher, kLiveSumPos, pprof));
  if (ctx.params.show_samples) {
    ddprof_print_sample(alloc_info.first, symbol_hdr, alloc_info.second._value,
                        kLiveSumPos, *watcher);
  }
  return ddres_init();
}

DDRes aggregate_live_allocations_for_pid(DDProfContext &ctx, pid_t pid) {
  struct UnwindState *us = ctx.worker_ctx.us;
  int const i_export = ctx.worker_ctx.i_current_pprof;
  DDProfPProf *pprof = ctx.worker_ctx.pprof[i_export];
  const SymbolHdr &symbol_hdr = us->symbol_hdr;
  LiveAllocation &live_allocations = ctx.worker_ctx.live_allocation;
  for (unsigned watcher_pos = 0;
       watcher_pos < live_allocations._watcher_vector.size(); ++watcher_pos) {
    auto &pid_map = live_allocations._watcher_vector[watcher_pos];
    const PerfWatcher *watcher = &ctx.watchers[watcher_pos];
    auto &pid_stacks = pid_map[pid];
    for (const auto &alloc_info : pid_stacks._unique_stacks) {
      DDRES_CHECK_FWD(aggregate_livealloc_stack(alloc_info, ctx, watcher, pprof,
                                                symbol_hdr));
    }
  }
  return ddres_init();
}

DDRes aggregate_live_allocations(DDProfContext &ctx) {
  // this would be more efficient if we could reuse the same stacks in
  // libdatadog
  struct UnwindState *us = ctx.worker_ctx.us;
  int const i_export = ctx.worker_ctx.i_current_pprof;
  DDProfPProf *pprof = ctx.worker_ctx.pprof[i_export];
  const SymbolHdr &symbol_hdr = us->symbol_hdr;
  const LiveAllocation &live_allocations = ctx.worker_ctx.live_allocation;
  for (unsigned watcher_pos = 0;
       watcher_pos < live_allocations._watcher_vector.size(); ++watcher_pos) {
    const auto &pid_map = live_allocations._watcher_vector[watcher_pos];
    const PerfWatcher *watcher = &ctx.watchers[watcher_pos];
    for (const auto &pid_vt : pid_map) {
      for (const auto &alloc_info : pid_vt.second._unique_stacks) {
        DDRES_CHECK_FWD(aggregate_livealloc_stack(alloc_info, ctx, watcher,
                                                  pprof, symbol_hdr));
      }
      LG_NTC("<%u> Number of Live allocations for PID%d=%lu, Unique stacks=%lu",
             watcher_pos, pid_vt.first, pid_vt.second._address_map.size(),
             pid_vt.second._unique_stacks.size());
    }
  }
  return ddres_init();
}

DDRes worker_pid_free(DDProfContext &ctx, pid_t el) {
  DDRES_CHECK_FWD(aggregate_live_allocations_for_pid(ctx, el));
  UnwindState *us = ctx.worker_ctx.us;
  unwind_pid_free(us, el);
  ctx.worker_ctx.live_allocation.clear_pid(el);
  return ddres_init();
}

DDRes clear_unvisited_pids(DDProfContext &ctx) {
  UnwindState *us = ctx.worker_ctx.us;
  const std::vector<pid_t> pids_remove = us->dwfl_hdr.get_unvisited();
  for (pid_t const el : pids_remove) {
    DDRES_CHECK_FWD(worker_pid_free(ctx, el));
  }
  us->dwfl_hdr.reset_unvisited();
  return ddres_init();
}

[[maybe_unused]] DDRes worker_init_stats(DDProfWorkerContext *worker_ctx) {
  DDRES_CHECK_FWD(proc_read(&worker_ctx->proc_status));
  worker_ctx->cycle_start_time = std::chrono::steady_clock::now();
  return {};
}

} // namespace

DDRes worker_library_init(DDProfContext &ctx,
                          PersistentWorkerState *persistent_worker_state) {
  try {
    // Set the initial time
    export_time_set(ctx);
    // Make sure worker-related counters are reset
    ctx.worker_ctx.count_worker = 0;
    // Make sure worker index is initialized correctly
    ctx.worker_ctx.i_current_pprof = 0;
    ctx.worker_ctx.exp_tid = {0};
    ctx.worker_ctx.us = new UnwindState(ctx.params.dd_profiling_fd);
    std::fill(ctx.worker_ctx.lost_events_per_watcher.begin(),
              ctx.worker_ctx.lost_events_per_watcher.end(), 0UL);

    // register the existing persistent storage for the state
    ctx.worker_ctx.persistent_worker_state = persistent_worker_state;

    PEventHdr *pevent_hdr = &ctx.worker_ctx.pevent_hdr;

    // If we're here, then we are a child spawned during the startup operation.
    // That means we need to iterate through the perf_event_open() handles and
    // get the mmaps
    if (!IsDDResOK(pevent_mmap(pevent_hdr, true))) {
      LG_NTC("Retrying attachment without user override");
      DDRES_CHECK_FWD(pevent_mmap(pevent_hdr, false));
    }
    // Initialize the unwind state and library
    unwind_init();
    ctx.worker_ctx.user_tags =
        new UserTags(ctx.params.tags, ctx.params.num_cpu);

    // Zero out pointers to dynamically allocated memory
    ctx.worker_ctx.exp[0] = nullptr;
    ctx.worker_ctx.exp[1] = nullptr;
    ctx.worker_ctx.pprof[0] = nullptr;
    ctx.worker_ctx.pprof[1] = nullptr;
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes worker_library_free(DDProfContext &ctx) {
  try {
    delete ctx.worker_ctx.user_tags;
    ctx.worker_ctx.user_tags = nullptr;

    PEventHdr *pevent_hdr = &ctx.worker_ctx.pevent_hdr;
    DDRES_CHECK_FWD(pevent_munmap(pevent_hdr));

    delete ctx.worker_ctx.us;
    ctx.worker_ctx.us = nullptr;
  }
  CatchExcept2DDRes();
  return ddres_init();
}

/************************* perf_event_open() helpers **************************/
/// Entry point for sample aggregation
DDRes ddprof_pr_sample(DDProfContext &ctx, perf_event_sample *sample,
                       int watcher_pos) {
  if (!sample) {
    return ddres_warn(DD_WHAT_PERFSAMP);
  }

  // If this is a SW_TASK_CLOCK-type event, then aggregate the time
  if (ctx.watchers[watcher_pos].config == PERF_COUNT_SW_TASK_CLOCK) {
    ddprof_stats_add(STATS_TARGET_CPU_USAGE, sample->period, nullptr);
  }

  auto ticks0 = get_tsc_cycles();
  DDRes const res = ddprof_unwind_sample(ctx, sample, watcher_pos);
  auto unwind_ticks = get_tsc_cycles();
  ddprof_stats_add(STATS_UNWIND_AVG_TIME, unwind_ticks - ticks0, nullptr);

  // Usually we want to send the sample_val, but sometimes we need to process
  // the event to get the desired value
  PerfWatcher *watcher = &ctx.watchers[watcher_pos];

  // Aggregate if unwinding went well (todo : fatal error propagation)
  if (!IsDDResFatal(res)) {
    struct UnwindState *us = ctx.worker_ctx.us;
    if (Any(EventAggregationMode::kLiveSum & watcher->aggregation_mode)) {
      ctx.worker_ctx.live_allocation.register_allocation(
          us->output, sample->addr, sample->period, watcher_pos, sample->pid);
    }
    if (Any(EventAggregationMode::kSum & watcher->aggregation_mode)) {
      // Depending on the type of watcher, compute a value for sample
      uint64_t const sample_val = perf_value_from_sample(watcher, sample);

      // in lib mode we don't aggregate (protect to avoid link failures)
      int const i_export = ctx.worker_ctx.i_current_pprof;
      DDProfPProf *pprof = ctx.worker_ctx.pprof[i_export];
      DDRES_CHECK_FWD(pprof_aggregate(&us->output, us->symbol_hdr, sample_val,
                                      1, watcher, kSumPos, pprof));
      if (ctx.params.show_samples) {
        ddprof_print_sample(us->output, us->symbol_hdr, sample->period, kSumPos,
                            *watcher);
      }
    }
  }

  ddprof_stats_add(STATS_AGGREGATION_AVG_TIME, get_tsc_cycles() - unwind_ticks,
                   nullptr);

  return {};
}

void *ddprof_worker_export_thread(void *arg) {
  auto *worker = static_cast<DDProfWorkerContext *>(arg);
  // export the one we are not writing to
  int const i = 1 - worker->i_current_pprof;
  // Increase number of sequences in persistent storage
  // This should start at 0, and if exporter thread
  // gets joined forcefully, we should not resume on same value
  uint32_t const profile_seq = (worker->persistent_worker_state->profile_seq)++;

  if (IsDDResFatal(ddprof_exporter_export(&worker->pprof[i]->_profile,
                                          worker->pprof[i]->_tags, profile_seq,
                                          worker->exp[i]))) {
    LG_NFO("Failed to export from worker");
    worker->exp_error = true;
  }

  return nullptr;
}

/// Cycle operations : export, sync metrics, update counters
DDRes ddprof_worker_cycle(DDProfContext &ctx,
                          std::chrono::steady_clock::time_point now,
                          [[maybe_unused]] bool synchronous_export) {

  // Clearing unused PIDs will ensure we don't report them at next cycle
  DDRES_CHECK_FWD(clear_unvisited_pids(ctx));
  DDRES_CHECK_FWD(aggregate_live_allocations(ctx));

  // Take the current pprof contents and ship them to the backend.  This also
  // clears the pprof for reuse
  // Dispatch happens in a thread, with the underlying data structure for
  // aggregation rotating between exports.  If we return to this point before
  // the previous thread has finished,  we wait for up to five seconds before
  // failing

  // If something is pending, return error
  if (ctx.worker_ctx.exp_tid) {
    struct timespec waittime;
    clock_gettime(CLOCK_REALTIME, &waittime);
    auto wait_sec =
        std::chrono::seconds{k_export_timeout - ctx.params.upload_period}
            .count();
    wait_sec = wait_sec > 1 ? wait_sec : 1;
    waittime.tv_sec += wait_sec;
    if (pthread_timedjoin_np(ctx.worker_ctx.exp_tid, nullptr, &waittime)) {
      LG_WRN("Exporter took too long");
      return ddres_create(DD_SEVERROR, DD_WHAT_EXPORT_TIMEOUT);
    }
    ctx.worker_ctx.exp_tid = 0;
  }
  if (ctx.worker_ctx.exp_error) {
    return ddres_create(DD_SEVERROR, DD_WHAT_EXPORTER);
  }

  DDRES_CHECK_FWD(report_lost_events(ctx));

  // Dispatch to thread
  ctx.worker_ctx.exp_error = false;

  // switch before we async export to avoid any possible race conditions (then
  // take into account the switch)
  ctx.worker_ctx.i_current_pprof = 1 - ctx.worker_ctx.i_current_pprof;

  // Reset the current, ensuring the timestamp starts when we are about to write
  // to it
  DDRES_CHECK_FWD(
      pprof_reset(ctx.worker_ctx.pprof[ctx.worker_ctx.i_current_pprof]));

  if (!synchronous_export) {
    pthread_create(&ctx.worker_ctx.exp_tid, nullptr,
                   ddprof_worker_export_thread, &ctx.worker_ctx);
  } else {
    ddprof_worker_export_thread(reinterpret_cast<void *>(&ctx.worker_ctx));
    if (ctx.worker_ctx.exp_error) {
      return ddres_create(DD_SEVERROR, DD_WHAT_EXPORTER);
    }
  }
  auto cycle_now = std::chrono::steady_clock::now();
  auto cycle_duration = cycle_now - ctx.worker_ctx.cycle_start_time;
  ctx.worker_ctx.cycle_start_time = cycle_now;

  // Scrape procfs for process usage statistics
  DDRES_CHECK_FWD(worker_update_stats(&ctx.worker_ctx.proc_status,
                                      *ctx.worker_ctx.us, cycle_duration));

  // And emit diagnostic output (if it's enabled)
  print_diagnostics(ctx.worker_ctx.us->dso_hdr);
  if (IsDDResNotOK(ddprof_stats_send(ctx.params.internal_stats))) {
    LG_WRN("Unable to utilize to statsd socket.  Suppressing future stats.");
    ctx.params.internal_stats = {};
  }

  // Increase the counts of exports
  ctx.worker_ctx.count_worker += 1;

  // allow new backpopulates
  ctx.worker_ctx.us->dso_hdr.reset_backpopulate_state();

  // Update the time last sent
  ctx.worker_ctx.send_time += ctx.params.upload_period;

  // If the clock was frozen for some reason, we need to detect situations
  // where we'll have catchup windows and reset the export timer.  This can
  // easily happen under temporary load when the profiler is off-CPU, if the
  // process is put in the cgroup freezer, or if we're being emulated.
  if (now > ctx.worker_ctx.send_time) {
    LG_WRN("Timer skew detected; frequent warnings may suggest system issue");
    export_time_set(ctx);
  }
  unwind_cycle(ctx.worker_ctx.us);

  // Reset stats relevant to a single cycle
  ddprof_reset_worker_stats();

  return ddres_init();
}

void ddprof_pr_mmap(DDProfContext &ctx, const perf_event_mmap2 *map,
                    int watcher_pos) {
  LG_DBG("<%d>(MAP)%d: %s (%lx/%lx/%lx) %c%c%c %02u:%02u %lu", watcher_pos,
         map->pid, map->filename, map->addr, map->len, map->pgoff,
         map->prot & PROT_READ ? 'r' : '-', map->prot & PROT_WRITE ? 'w' : '-',
         map->prot & PROT_EXEC ? 'x' : '-', map->maj, map->min, map->ino);
  Dso new_dso(map->pid, map->addr, map->addr + map->len - 1, map->pgoff,
              std::string(map->filename), map->ino, map->prot);
  ctx.worker_ctx.us->dso_hdr.insert_erase_overlap(std::move(new_dso));
}

void ddprof_pr_lost(DDProfContext &ctx, const perf_event_lost *lost,
                    int watcher_pos) {
  ddprof_stats_add(STATS_EVENT_LOST, lost->lost, nullptr);
  ctx.worker_ctx.lost_events_per_watcher[watcher_pos] += lost->lost;
}

DDRes ddprof_pr_comm(DDProfContext &ctx, const perf_event_comm *comm,
                     int watcher_pos) {
  // Change in process name (assuming exec) : clear all associated dso
  if (comm->header.misc & PERF_RECORD_MISC_COMM_EXEC) {
    LG_DBG("<%d>(COMM)%d -> %s", watcher_pos, comm->pid, comm->comm);
    DDRES_CHECK_FWD(worker_pid_free(ctx, comm->pid));
  }
  return ddres_init();
}

DDRes ddprof_pr_fork(DDProfContext &ctx, const perf_event_fork *frk,
                     int watcher_pos) {
  LG_DBG("<%d>(FORK)%d -> %d/%d", watcher_pos, frk->ppid, frk->pid, frk->tid);
  if (frk->ppid != frk->pid) {
    // Clear everything and populate at next error or with coming samples
    DDRES_CHECK_FWD(worker_pid_free(ctx, frk->pid));
    ctx.worker_ctx.us->dso_hdr.pid_fork(frk->pid, frk->ppid);
  }
  return ddres_init();
}

void ddprof_pr_exit(DDProfContext &ctx, const perf_event_exit *ext,
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

void ddprof_pr_clear_live_allocation(DDProfContext &ctx,
                                     const ClearLiveAllocationEvent *event,
                                     int watcher_pos) {
  LG_DBG("<%d>(CLEAR LIVE)%d", watcher_pos, event->sample_id.pid);
  ctx.worker_ctx.live_allocation.clear_pid_for_watcher(watcher_pos,
                                                       event->sample_id.pid);
}

void ddprof_pr_deallocation(DDProfContext &ctx, const DeallocationEvent *event,
                            int watcher_pos) {
  ctx.worker_ctx.live_allocation.register_deallocation(event->ptr, watcher_pos,
                                                       event->sample_id.pid);
}

/********************************** callbacks *********************************/
DDRes ddprof_worker_maybe_export(DDProfContext &ctx,
                                 std::chrono::steady_clock::time_point now) {
  try {
    if (now > ctx.worker_ctx.send_time) {
      // restart worker if number of uploads is reached
      ctx.worker_ctx.persistent_worker_state->restart_worker =
          (ctx.worker_ctx.count_worker + 1 >= ctx.params.worker_period);
      // when restarting worker, do a synchronous export
      DDRES_CHECK_FWD(ddprof_worker_cycle(
          ctx, now, ctx.worker_ctx.persistent_worker_state->restart_worker));
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes ddprof_worker_init(DDProfContext &ctx,
                         PersistentWorkerState *persistent_worker_state) {
  try {
    DDRES_CHECK_FWD(worker_library_init(ctx, persistent_worker_state));
    ctx.worker_ctx.exp[0] = new DDProfExporter();
    ctx.worker_ctx.exp[1] = new DDProfExporter();
    ctx.worker_ctx.pprof[0] = new DDProfPProf();
    ctx.worker_ctx.pprof[1] = new DDProfPProf();

    DDRES_CHECK_FWD(ddprof_exporter_init(ctx.exp_input, ctx.worker_ctx.exp[0]));
    DDRES_CHECK_FWD(ddprof_exporter_init(ctx.exp_input, ctx.worker_ctx.exp[1]));
    // warning : depends on unwind init
    DDRES_CHECK_FWD(
        ddprof_exporter_new(ctx.worker_ctx.user_tags, ctx.worker_ctx.exp[0]));
    DDRES_CHECK_FWD(
        ddprof_exporter_new(ctx.worker_ctx.user_tags, ctx.worker_ctx.exp[1]));

    DDRES_CHECK_FWD(pprof_create_profile(ctx.worker_ctx.pprof[0], ctx));
    DDRES_CHECK_FWD(pprof_create_profile(ctx.worker_ctx.pprof[1], ctx));
    DDRES_CHECK_FWD(worker_init_stats(&ctx.worker_ctx));
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes ddprof_worker_free(DDProfContext &ctx) {
  try {
    // First, see if there are any outstanding requests and give them a token
    // amount of time to complete
    if (ctx.worker_ctx.exp_tid) {
      struct timespec waittime;
      clock_gettime(CLOCK_REALTIME, &waittime);
      constexpr std::chrono::seconds k_export_thread_join_timeout{5};
      waittime.tv_sec +=
          std::chrono::seconds(k_export_thread_join_timeout).count();
      if (pthread_timedjoin_np(ctx.worker_ctx.exp_tid, nullptr, &waittime)) {
        pthread_cancel(ctx.worker_ctx.exp_tid);
      }
      ctx.worker_ctx.exp_tid = 0;
    }

    DDRES_CHECK_FWD(worker_library_free(ctx));
    for (int i = 0; i < 2; i++) {
      if (ctx.worker_ctx.exp[i]) {
        DDRES_CHECK_FWD(ddprof_exporter_free(ctx.worker_ctx.exp[i]));
        delete ctx.worker_ctx.exp[i];
        ctx.worker_ctx.exp[i] = nullptr;
      }
      if (ctx.worker_ctx.pprof[i]) {
        DDRES_CHECK_FWD(pprof_free_profile(ctx.worker_ctx.pprof[i]));
        delete ctx.worker_ctx.pprof[i];
        ctx.worker_ctx.pprof[i] = nullptr;
      }
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}

// Simple wrapper over perf_event_hdr in order to filter by PID in a uniform
// way.  Whenever PID is a valid concept for the given event type, the
// interface uniformly presents PID as the element immediately after the
// header.
struct perf_event_hdr_wpid : perf_event_header {
  uint32_t pid, tid;
};

DDRes ddprof_worker_process_event(const perf_event_header *hdr, int watcher_pos,
                                  DDProfContext &ctx) {
  // global try catch to avoid leaking exceptions to main loop
  try {
    ddprof_stats_add(STATS_EVENT_COUNT, 1, nullptr);
    const auto *wpid = static_cast<const perf_event_hdr_wpid *>(hdr);
    PerfWatcher *watcher = &ctx.watchers[watcher_pos];
    switch (hdr->type) {
    /* Cases where the target type has a PID */
    case PERF_RECORD_SAMPLE:
      if (wpid->pid) {
        uint64_t const mask = watcher->sample_type;
        perf_event_sample *sample = hdr2samp(hdr, mask);
        if (sample) {
          DDRES_CHECK_FWD(ddprof_pr_sample(ctx, sample, watcher_pos));
        }
      }
      break;
    case PERF_RECORD_MMAP2:
      if (wpid->pid) {
        ddprof_pr_mmap(ctx, reinterpret_cast<const perf_event_mmap2 *>(hdr),
                       watcher_pos);
      }
      break;
    case PERF_RECORD_COMM:
      if (wpid->pid) {
        DDRES_CHECK_FWD(ddprof_pr_comm(
            ctx, reinterpret_cast<const perf_event_comm *>(hdr), watcher_pos));
      }
      break;
    case PERF_RECORD_EXIT:
      if (wpid->pid) {
        ddprof_pr_exit(ctx, reinterpret_cast<const perf_event_exit *>(hdr),
                       watcher_pos);
      }
      break;
    case PERF_RECORD_FORK:
      if (wpid->pid) {
        DDRES_CHECK_FWD(ddprof_pr_fork(
            ctx, reinterpret_cast<const perf_event_fork *>(hdr), watcher_pos));
      }

      break;

    /* Cases where the target type might not have a PID */
    case PERF_RECORD_LOST:
      ddprof_pr_lost(ctx, reinterpret_cast<const perf_event_lost *>(hdr),
                     watcher_pos);
      break;
    case PERF_CUSTOM_EVENT_DEALLOCATION:
      ddprof_pr_deallocation(
          ctx, reinterpret_cast<const DeallocationEvent *>(hdr), watcher_pos);
      break;
    case PERF_CUSTOM_EVENT_CLEAR_LIVE_ALLOCATION: {
      const auto *event =
          reinterpret_cast<const ClearLiveAllocationEvent *>(hdr);
      DDRES_CHECK_FWD(
          aggregate_live_allocations_for_pid(ctx, event->sample_id.pid));
      ddprof_pr_clear_live_allocation(ctx, event, watcher_pos);
    } break;
    default:
      break;
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}
} // namespace ddprof
