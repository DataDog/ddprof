// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct PerfWatcherOptions {
  bool is_kernel;
  bool is_freq;
  bool is_raw;
};

typedef struct PerfWatcher {
  const char *desc;
  int type;
  unsigned long config;
  union {
    uint64_t sample_period;
    uint64_t sample_frequency;
  };
  int profile_id;
  // perf_event_open configs
  struct PerfWatcherOptions options;
  // tracepoint configuration
  uint64_t id;
  uint8_t reg;
  uint8_t trace_off;
  uint8_t trace_sz;
} PerfWatcher;

// The Datadog backend only understands pre-configured event types.  Those
// types are defined here, and then referenced in the watcher
#define PROFILE_TYPE_TABLE(X) \
  X(TRACEPOINT, tracepoint, events) \
  X(CPU_NANOS,  cpu-time,   nanoseconds) \
  X(CPU_SAMPLE, cpu-sample, count)

#define X_ENUM(a, b, c) DDPROF_PWT_##a,
typedef enum DDPROF_SAMPLE_TYPES {
  PROFILE_TYPE_TABLE(X_ENUM)
  DDPROF_PWT_LENGTH,
} DDPROF_SAMPLE_TYPES;
#undef X_ENUM

enum DDPROF_PERFOPEN_CONFIGS {
  IS_FREQ = 1<<0,
  IS_KERNEL = 1<<1,
};

// Whereas tracepoints are dynamically configured and can be checked at runtime,
// we lack the ability to inspect events of type other than TYPE_TRACEPOINT.
// Accordingly, we maintain a list of events
// clang-format off
//  short    desc              type config                   period/freq  profile type   addtl. configs
#define EVENT_CONFIG_TABLE(X) \
  X(hCPU,    "CPU Cycles",      PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES,              99,   DDPROF_PWT_TRACEPOINT, IS_FREQ)   \
  X(hREF,    "Ref. CPU Cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_REF_CPU_CYCLES,          1000, DDPROF_PWT_TRACEPOINT, IS_FREQ)   \
  X(hINST,   "Instr. Count",    PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS,            1000, DDPROF_PWT_TRACEPOINT, IS_FREQ)   \
  X(hCREF,   "Cache Ref.",      PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES,        999,  DDPROF_PWT_TRACEPOINT, 0)         \
  X(hCMISS,  "Cache Miss",      PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES,            999,  DDPROF_PWT_TRACEPOINT, 0)         \
  X(hBRANCH, "Branche Instr.",  PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS,     999,  DDPROF_PWT_TRACEPOINT, 0)         \
  X(hBMISS,  "Branch Miss",     PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES,           999,  DDPROF_PWT_TRACEPOINT, 0)         \
  X(hBUS,    "Bus Cycles",      PERF_TYPE_HARDWARE, PERF_COUNT_HW_BUS_CYCLES,              1000, DDPROF_PWT_TRACEPOINT, IS_FREQ)   \
  X(hBSTF,   "Bus Stalls(F)",   PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_FRONTEND, 1000, DDPROF_PWT_TRACEPOINT, IS_FREQ)   \
  X(hBSTB,   "Bus Stalls(B)",   PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_BACKEND,  1000, DDPROF_PWT_TRACEPOINT, IS_FREQ)   \
  X(sCPU,    "CPU Time",        PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK,              99,   DDPROF_PWT_CPU_NANOS,  IS_FREQ)   \
  X(sPF,     "Page Faults",     PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS,             1,    DDPROF_PWT_TRACEPOINT, IS_KERNEL) \
  X(sCS,     "Con. Switch",     PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES,        1,    DDPROF_PWT_TRACEPOINT, IS_KERNEL) \
  X(sMig,    "CPU Migrations",  PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS,          99,   DDPROF_PWT_TRACEPOINT, IS_FREQ)   \
  X(sPFMAJ,  "Minor Faults",    PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MIN,         99,   DDPROF_PWT_TRACEPOINT, IS_KERNEL) \
  X(sPFMIN,  "Major Faults",    PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MAJ,         99,   DDPROF_PWT_TRACEPOINT, IS_KERNEL) \
  X(sALGN,   "Align. Faults",   PERF_TYPE_SOFTWARE, PERF_COUNT_SW_ALIGNMENT_FAULTS,        99,   DDPROF_PWT_TRACEPOINT, IS_FREQ)   \
  X(sEMU,    "Emu. Faults",     PERF_TYPE_SOFTWARE, PERF_COUNT_SW_EMULATION_FAULTS,        99,   DDPROF_PWT_TRACEPOINT, IS_FREQ)   \
  X(sDUM,    "Dummy",           PERF_TYPE_SOFTWARE, PERF_COUNT_SW_DUMMY,                   1,    DDPROF_PWT_TRACEPOINT, IS_FREQ)
// clang-format on

#define X_ENUM(a, b, c, d, e, f, g) DDPROF_PWE_##a,
typedef enum DDPROF_EVENT_NAMES {
  EVENT_CONFIG_TABLE(X_ENUM)
  DDPROF_PWE_LENGTH,
} DDPROF_EVENT_NAMES;
#undef X_ENUM

// Helper functions for event-type watcher lookups
const PerfWatcher *ewatcher_from_idx(int idx);
const PerfWatcher *ewatcher_from_str(const char *str);
const PerfWatcher *twatcher_default();

// Helper functions for profile types
const char *profile_name_from_idx(int idx);
const char *profile_unit_from_idx(int idx);
