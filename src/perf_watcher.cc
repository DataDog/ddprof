// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf_watcher.hpp"

#include "logger.hpp"
#include "perf.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define BASE_STYPES                                                            \
  (PERF_SAMPLE_STACK_USER | PERF_SAMPLE_REGS_USER | PERF_SAMPLE_TID |          \
   PERF_SAMPLE_TIME | PERF_SAMPLE_PERIOD)

uint64_t perf_event_default_sample_type() { return BASE_STYPES; }

#define X_STR(a, b, c, d) b,
const char *sample_type_name_from_idx(int idx) {
  static const char *sample_names[] = {PROFILE_TYPE_TABLE(X_STR)};
  if (idx < 0 || idx >= DDPROF_PWT_LENGTH)
    return NULL;
  return sample_names[idx];
}
#undef X_STR
#define X_STR(a, b, c, d) #c,
const char *sample_type_unit_from_idx(int idx) {
  static const char *sample_units[] = {PROFILE_TYPE_TABLE(X_STR)};
  if (idx < 0 || idx >= DDPROF_PWT_LENGTH)
    return NULL;
  return sample_units[idx];
}
#undef X_STR
#define X_DEP(a, b, c, d) DDPROF_PWT_##d,
int sample_type_id_to_count_sample_type_id(int idx) {
  static const int count_ids[] = {PROFILE_TYPE_TABLE(X_DEP)};
  if (idx < 0 || idx >= DDPROF_PWT_LENGTH)
    return DDPROF_PWT_NOCOUNT;
  return count_ids[idx];
}
#undef X_DEP

int watcher_to_count_sample_type_id(const PerfWatcher *watcher) {
  int idx = watcher->sample_type_id;
  return sample_type_id_to_count_sample_type_id(idx);
}

bool watcher_has_countable_sample_type(const PerfWatcher *watcher) {
  return DDPROF_PWT_NOCOUNT != watcher_to_count_sample_type_id(watcher);
}

#define X_EVENTS(a, b, c, d, e, f, g)                                          \
  {DDPROF_PWE_##a, b, BASE_STYPES, c, d, {e}, f, PERF_SAMPLE_STACK_SIZE, g},

#define X_STR(a, b, c, d, e, f, g) #a,
const char *event_type_name_from_idx(int idx) {
  static const char *event_names[] = {EVENT_CONFIG_TABLE(X_STR)}; // NOLINT
  if (idx < 0 || idx >= DDPROF_PWE_LENGTH)
    return NULL;
  return event_names[idx];
}
#undef X_STR

int str_to_event_idx(const char *str) {
  if (!str || !*str)
    return -1;
  size_t sz_input = strlen(str);
  for (int type = 0; type < DDPROF_PWE_LENGTH; ++type) {
    const char *event_name = event_type_name_from_idx(type);
    size_t sz_this = strlen(event_name);
    if (sz_input == sz_this && !strncmp(str, event_name, sz_this))
      return type;
  }
  return -1;
}

const PerfWatcher *ewatcher_from_idx(int idx) {
  if (idx < 0 || idx >= DDPROF_PWE_LENGTH)
    return NULL;
  static const PerfWatcher events[] = {EVENT_CONFIG_TABLE(X_EVENTS)};
  return &events[idx];
}

const PerfWatcher *ewatcher_from_str(const char *str) {
  return ewatcher_from_idx(str_to_event_idx(str));
}

const PerfWatcher *tracepoint_default_watcher() {
  static const PerfWatcher tracepoint_template = {
      .ddprof_event_type = DDPROF_PWE_TRACEPOINT,
      .desc = "Tracepoint",
      .sample_type = BASE_STYPES,
      .type = PERF_TYPE_TRACEPOINT,
      .sample_period = 1,
      .sample_type_id = DDPROF_PWT_TRACEPOINT,
      .options = {.use_kernel = PerfWatcherUseKernel::kRequired},
      .value_scale = 1.0,
  };
  return &tracepoint_template;
}

bool watcher_has_tracepoint(const PerfWatcher *watcher) {
  return DDPROF_PWT_TRACEPOINT == watcher->sample_type_id;
}

const char *get_tracefs_root() {
  static const char* tracepoint_root = NULL;
  constexpr std::array<std::string_view, 2> candidate_paths = {
    "/sys/kernel/tracing/events/",
    "/sys/kernel/debug/tracing/events"
  };

  if (!tracepoint_root) {
    struct stat sa;
    for (auto &candidate : candidate_paths) {
      if (!stat(candidate.data(), &sa))
        return tracepoint_root = candidate.data();
    }
    // If none of them worked, then it failed.
    // Log the message.
    LG_WRN("Could not determine tracefs root");
  }
  return tracepoint_root;
}

unsigned int tracepoint_id_from_event(const char *eventname,
                                      const char *groupname) {
  if (!eventname || !*eventname || !groupname || !*groupname)
    return 0;

  static char path[4096]; // Arbitrary, but path sizes limits are difficult
  static char buf[sizeof("4294967296")]; // For reading 32-bit decimal int
  const char *tracefs_root = get_tracefs_root();
  if (!tracefs_root) {
    return 0;
  }
  char *buf_copy = buf;
  size_t pathsz =
      snprintf(path, sizeof(path), "%s/%s/%s/id",
               tracefs_root, groupname, eventname);
  if (pathsz >= sizeof(path))
    return 0;
  int fd = open(path, O_RDONLY);
  if (-1 == fd)
    return 0;

  // Read the data in an eintr-safe way
  int read_ret = -1;
  long trace_id = 0;
  do {
    read_ret = read(fd, buf, sizeof(buf));
  } while (read_ret == -1 && errno == EINTR);
  close(fd);
  if (read_ret > 0)
    trace_id = strtol(buf, &buf_copy, 10);
  if (*buf_copy && *buf_copy != '\n')
    return 0;

  return trace_id;
}

