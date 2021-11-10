// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf_option.h"

#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>

#include <stddef.h>

// clang-format off
static const PerfOption perfoptions[] = {
  // Hardware
  {"CPU Cycles",      PERF_TYPE_HARDWARE, {PERF_COUNT_HW_CPU_CYCLES},              {99},   "cpu-cycle",      "cycles", .freq = true},
  {"Ref. CPU Cycles", PERF_TYPE_HARDWARE, {PERF_COUNT_HW_REF_CPU_CYCLES},          {1000}, "ref-cycle",      "cycles", .freq = true},
  {"Instr. Count",    PERF_TYPE_HARDWARE, {PERF_COUNT_HW_INSTRUCTIONS},            {1000}, "cpu-instr",      "instructions", .freq = true},
  {"Cache Ref.",      PERF_TYPE_HARDWARE, {PERF_COUNT_HW_CACHE_REFERENCES},        {1000}, "cache-ref",      "events"},
  {"Cache Miss",      PERF_TYPE_HARDWARE, {PERF_COUNT_HW_CACHE_MISSES},            {1000}, "cache-miss",     "events"},
  {"Branche Instr.",  PERF_TYPE_HARDWARE, {PERF_COUNT_HW_BRANCH_INSTRUCTIONS},     {1000}, "branch-instr",   "events"},
  {"Branch Miss",     PERF_TYPE_HARDWARE, {PERF_COUNT_HW_BRANCH_MISSES},           {1000}, "branch-miss",    "events"},
  {"Bus Cycles",      PERF_TYPE_HARDWARE, {PERF_COUNT_HW_BUS_CYCLES},              {1000}, "bus-cycle",      "cycles", .freq = true},
  {"Bus Stalls(F)",   PERF_TYPE_HARDWARE, {PERF_COUNT_HW_STALLED_CYCLES_FRONTEND}, {1000}, "bus-stf",        "cycles", .freq = true},
  {"Bus Stalls(B)",   PERF_TYPE_HARDWARE, {PERF_COUNT_HW_STALLED_CYCLES_BACKEND},  {1000}, "bus-stb",        "cycles", .freq = true},
  {"CPU Time",        PERF_TYPE_SOFTWARE, {PERF_COUNT_SW_TASK_CLOCK},              {99},   "cpu-time",       "nanoseconds", .freq = true},
  {"Wall? Time",      PERF_TYPE_SOFTWARE, {PERF_COUNT_SW_CPU_CLOCK},               {99},   "wall-time",      "nanoseconds", .freq = true},
  {"Ctext Switches",  PERF_TYPE_SOFTWARE, {PERF_COUNT_SW_CONTEXT_SWITCHES},        {1},    "switches",       "events", .include_kernel = true},
  {"Block-Insert",    PERF_TYPE_TRACEPOINT, {1133},                                {1},    "block-insert",   "events", .include_kernel = true},
  {"Block-Issue",     PERF_TYPE_TRACEPOINT, {1132},                                {1},    "block-issue",    "events", .include_kernel = true},
  {"Block-Complete",  PERF_TYPE_TRACEPOINT, {1134},                                {1},    "block-complete", "events", .include_kernel = true},
  {"Malloc",          PERF_TYPE_BREAKPOINT, {0},                                   {1},    "malloc",         "events", .bp_type = HW_BREAKPOINT_X},
};
// clang-format on

// There's a strong assumption here that these elements match up perfectly with
// the table below.  This will be a bit of a pain to maintain.
static const char *perfoptions_lookup_table[] = {
    "hCPU",   "hREF",  "hINSTR", "hCREF", "hCMISS", "hBRANCH",
    "hBMISS", "hBUS",  "hBSTF",  "hBSTB", "sCPU",   "sWALL",
    "sCI",    "kBLKI", "kBLKS",  "kBLKC", "bMalloc"};

#define perfoptions_sz (sizeof(perfoptions) / sizeof(*perfoptions))
#define perfoptions_lookup_sz                                                  \
  (sizeof(perfoptions_lookup_table) / sizeof(*perfoptions_lookup_table))

const PerfOption *perfoptions_preset(int idx) {
  if (idx >= perfoptions_sz || idx < 0) {
    return NULL;
  }
  return &perfoptions[idx];
}

int perfoptions_nb_presets(void) { return perfoptions_sz; }

const char *perfoptions_lookup_idx(int idx) {
  return perfoptions_lookup_table[idx];
}

const char **perfoptions_lookup(void) { return perfoptions_lookup_table; }

// check that size match
bool perfoptions_match_size(void) {
  return perfoptions_lookup_sz == perfoptions_sz;
}