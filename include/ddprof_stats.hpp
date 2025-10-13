// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres.hpp"
#include "logger.hpp"
#include "statsd.hpp"

namespace ddprof {

#define X_ENUM(a, b, c) STATS_##a,
#define STATS_TABLE(X)                                                         \
  X(EVENT_COUNT, "event.count", STAT_GAUGE)                                    \
  X(EVENT_LOST, "event.lost", STAT_GAUGE)                                      \
  X(EVENT_DEALLOC_LOST, "event.dealloc_lost", STAT_GAUGE)                      \
  X(EVENT_OUT_OF_ORDER, "event.out_of_order", STAT_GAUGE)                      \
  X(SAMPLE_COUNT, "sample.count", STAT_GAUGE)                                  \
  X(UNMATCHED_DEALLOCATION_COUNT, "unmatched_deallocation.count", STAT_GAUGE)  \
  X(ALREADY_EXISTING_ALLOCATION_COUNT, "already_existing_allocation.count",    \
    STAT_GAUGE)                                                                \
  X(TARGET_CPU_USAGE, "target_process.cpu_usage.millicores", STAT_GAUGE)       \
  X(UNWIND_AVG_TIME, "unwind.avg_time_ns", STAT_GAUGE)                         \
  X(UNWIND_FRAMES, "unwind.frames", STAT_GAUGE)                                \
  X(UNWIND_ERRORS, "unwind.errors", STAT_GAUGE)                                \
  X(UNWIND_TRUNCATED_INPUT, "unwind.stack.truncated_input", STAT_GAUGE)        \
  X(UNWIND_TRUNCATED_OUTPUT, "unwind.stack.truncated_output", STAT_GAUGE)      \
  X(UNWIND_INCOMPLETE_STACK, "unwind.stack.incomplete", STAT_GAUGE)            \
  X(UNWIND_AVG_STACK_SIZE, "unwind.stack.avg_size", STAT_GAUGE)                \
  X(UNWIND_AVG_STACK_DEPTH, "unwind.stack.avg_depth", STAT_GAUGE)              \
  X(UNUSED_SYMBOLS_BINARIES_COUNT, "symbols.binaries.unused.count",            \
    STAT_GAUGE)                                                                \
  X(SYMBOLS_JIT_READS, "symbols.jit.reads", STAT_GAUGE)                        \
  X(SYMBOLS_JIT_FAILED_LOOKUPS, "symbols.jit.failed_lookups", STAT_GAUGE)      \
  X(SYMBOLS_JIT_SYMBOL_COUNT, "symbols.jit.symbol_count", STAT_GAUGE)          \
  X(PROFILER_RSS, "profiler.rss", STAT_GAUGE)                                  \
  X(PROFILER_CPU_USAGE, "profiler.cpu_usage.millicores", STAT_GAUGE)           \
  X(DSO_NEW_DSO, "dso.new", STAT_GAUGE)                                        \
  X(DSO_SIZE, "dso.size", STAT_GAUGE)                                          \
  X(PPROF_SIZE, "pprof.size", STAT_GAUGE)                                      \
  X(PROFILE_DURATION, "profile.duration_ms", STAT_GAUGE)                       \
  X(AGGREGATION_AVG_TIME, "aggregation.avg_time_ns", STAT_GAUGE)               \
  X(BACKPOPULATE_COUNT, "backpopulate.count", STAT_GAUGE)

// Expand the enum/index for the individual stats
enum DDPROF_STATS : uint8_t { STATS_TABLE(X_ENUM) STATS_LEN };
#undef X_ENUM

// Necessary for initializing the backend store for stats.  It's necessary that
// this is called prior to any fork() calls where the children might want to use
// stats, but it's fine to call this after forks have spawned.
DDRes ddprof_stats_init();

// Whereas the regions are inherited by forks, freeing the backend store is not.
// That means that free() must be called from every process wishing to clean
// its own store (although this is a small array of longs, so probably not too
// important)
DDRes ddprof_stats_free();

// The add operator is multithread- and multiprocess-safe.  `out` can be NULL.
DDRes ddprof_stats_add(unsigned int stat, long in, long *out);

DDRes ddprof_stats_divide(unsigned int stat, long n);

// Setting and clearing are last-through-the-gate operations with ties broken
// by whatever the CPU is executing at that time.
DDRes ddprof_stats_set(unsigned int stat, long in);
DDRes ddprof_stats_clear(unsigned int stat);

// Merely gets the value of the statistic.
DDRes ddprof_stats_get(unsigned int stat, long *out);

// Send all the registered values
DDRes ddprof_stats_send(std::string_view statsd_socket);

// Print all known stats to the configured log
void ddprof_stats_print();

} // namespace ddprof
