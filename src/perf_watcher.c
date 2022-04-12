// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf_watcher.h"

#include <stddef.h>
#include <string.h>

#define BASE_STYPES                                                            \
  (PERF_SAMPLE_STACK_USER | PERF_SAMPLE_REGS_USER | PERF_SAMPLE_TID |          \
   PERF_SAMPLE_TIME | PERF_SAMPLE_PERIOD)

uint64_t perf_event_default_sample_type() { return BASE_STYPES; }

#define X_STR(a, b, c, d) b,
const char *profile_name_from_idx(int idx) {
  static const char *sample_names[] = {PROFILE_TYPE_TABLE(X_STR)};
  if (idx < 0 || idx >= DDPROF_PWT_LENGTH)
    return NULL;
  return sample_names[idx];
}
#undef X_STR
#define X_STR(a, b, c, d) #c,
const char *profile_unit_from_idx(int idx) {
  static const char *sample_units[] = {PROFILE_TYPE_TABLE(X_STR)};
  if (idx < 0 || idx >= DDPROF_PWT_LENGTH)
    return NULL;
  return sample_units[idx];
}
#undef X_STR
#define X_DEP(a, b, c, d) DDPROF_PWT_##d,
int watcher_to_count_id(const PerfWatcher *watcher) {
  static const int count_id[] = {PROFILE_TYPE_TABLE(X_DEP)};
  int idx = watcher->sample_type_id;
  if (idx < 0 || idx >= DDPROF_PWT_LENGTH)
    return DDPROF_PWT_NOCOUNT;
  return count_id[idx];
}
#undef X_DEP

bool watcher_has_countable_sample_type(const PerfWatcher *watcher) {
  return DDPROF_PWT_NOCOUNT != watcher_to_count_id(watcher);
}

#define BITS2OPT(b)                                                            \
  ((struct PerfWatcherOptions){.is_kernel = (b)&IS_KERNEL,                     \
                               .is_freq = (b)&IS_FREQ})

#define X_EVENTS(a, b, c, d, e, f, g) {b, BASE_STYPES, c, d, e, f, BITS2OPT(g)},
const PerfWatcher events_templates[] = {EVENT_CONFIG_TABLE(X_EVENTS)};
const PerfWatcher tracepoint_templates[] = {{
    .desc = "Tracepoint",
    .type = PERF_TYPE_TRACEPOINT,
    .sample_period = 1,
    .sample_type = BASE_STYPES,
    .sample_type_id = DDPROF_PWT_TRACEPOINT,
    .options = {.is_kernel = true},
}};
#undef X_PWATCH

#define X_STR(a, b, c, d, e, f, g) #a,
int str_to_event_idx(const char *str) {
  static const char *event_names[] = {EVENT_CONFIG_TABLE(X_STR)}; // NOLINT
  int type;
  if (!str)
    return -1;
  size_t sz_str = strlen(str);
  for (type = 0; type < DDPROF_PWE_LENGTH; ++type) {
    size_t sz_thistype = strlen(event_names[type]);

    // We don't want to match partial events, and the event specification
    // demands that events are either whole or immediately preceeded by a comma.
    if (sz_str < sz_thistype)
      continue;
    else if (sz_str > sz_thistype && str[sz_thistype] != ',')
      continue;
    if (!strncmp(str, event_names[type], sz_thistype))
      return type;
  }
  return -1;
}
#undef X_STR

const PerfWatcher *ewatcher_from_idx(int idx) {
  if (idx < 0 || idx >= DDPROF_PWE_LENGTH)
    return NULL;
  return &events_templates[idx];
}

const PerfWatcher *ewatcher_from_str(const char *str) {
  return ewatcher_from_idx(str_to_event_idx(str));
}

const PerfWatcher *twatcher_default() {
  // Only the one (for now?!)
  return &tracepoint_templates[0];
}

bool watcher_has_tracepoint(const PerfWatcher *watcher) {
  return DDPROF_PWT_TRACEPOINT == watcher->sample_type_id;
}
