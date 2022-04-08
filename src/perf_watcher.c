// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf_watcher.h"

#include <stddef.h>
#include <string.h>


#define X_STR(a, b, c, d) #b,
const char *profile_name_from_idx(int idx) {
  static const char* sample_names[] = { PROFILE_TYPE_TABLE(X_STR) };
  if (idx < 0 && idx >= DDPROF_PWT_LENGTH)
    return NULL;
  return sample_names[idx];
}
#undef X_STR
#define X_STR(a, b, c, d) #c,
const char *profile_unit_from_idx(int idx) {
  static const char* sample_units[] = { PROFILE_TYPE_TABLE(X_STR) };
  if (idx < 0 && idx >= DDPROF_PWT_LENGTH)
    return NULL;
  return sample_units[idx];
}
#undef X_STR

#define BITS2OPT(b) ((struct PerfWatcherOptions){ \
    .is_kernel = (b) & IS_KERNEL, \
    .is_freq = (b) & IS_FREQ})

#define X_EVENTS(a, b, c, d, e, f, g) {b, c, d, e, f, BITS2OPT(g)},
const PerfWatcher events_templates[] = { EVENT_CONFIG_TABLE(X_EVENTS) };
const PerfWatcher tracepoint_templates[] = {{
  .type = PERF_TYPE_TRACEPOINT,
  .sample_period = 1,
  .profile_id = DDPROF_PWT_TRACEPOINT,
  .options = {.is_kernel = true},
}};
#undef X_PWATCH

#define X_STR(a, b, c, d, e, f, g) #a,
int str_to_event_idx(const char *str) {
  static const char* event_input_names[] = { EVENT_CONFIG_TABLE(X_STR) };
  int type;
  if (!str)
    return -1;
  for (type = 0; type < DDPROF_PWE_LENGTH; ++type) {
    if (!strcmp(str, event_input_names[type]))
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
