// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_cmdline.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ddres_helpers.h"
#include "perf_archmap.h"
#include "perf_watcher.h"

int arg_which(const char *str, char const *const *set, int sz_set) {
  if (!str || !set)
    return -1;
  for (int i = 0; i < sz_set; i++) {
    if (set[i] && !strcasecmp(str, set[i]))
      return i;
  }
  return -1;
}

bool arg_inset(const char *str, char const *const *set, int sz_set) {
  int ret = arg_which(str, set, sz_set);
  return !(-1 == ret);
}

bool arg_yesno(const char *str, int mode) {
  const int sizeOfPatterns = 3;
  static const char *yes_set[] = {"yes", "true", "on"}; // mode = 1
  static const char *no_set[] = {"no", "false", "off"}; // mode = 0
  assert(0 == mode || 1 == mode);
  char const *const *set = (!mode) ? no_set : yes_set;
  if (arg_which(str, set, sizeOfPatterns) != -1) {
    return true;
  }
  return false;
}

// If this returns false, then the passed watcher should be regarded as invalid
bool watcher_from_event(const char *str, PerfWatcher *watcher) {
  const PerfWatcher *tmp_watcher;
  if (!(tmp_watcher = ewatcher_from_str(str)))
    return false;

  // Now we have to process options out of the string
  char *str_chk;                    // for checking the result of parsing
  char *str_tmp = strchr(str, ','); // points to ',' or is nullptr
  uint64_t value_tmp = tmp_watcher->sample_period; // get default val

  if (str_tmp) {
    ++str_tmp; // nav to after ','
    value_tmp = strtoll(str_tmp, &str_chk, 10);
    if (*str_chk)
      return false; // If this is malformed, the whole thing is malformed?
  }

  // If we're here, then we've processed the event specification correctly, so
  // we copy the tmp_watcher into the storage given by the user and update the
  // mutable field
  *watcher = *tmp_watcher;
  watcher->sample_period = value_tmp;

  // If an event doesn't have a well-defined profile type, then it gets
  // registered as a tracepoint profile.  Make sure it has a valid name for the
  // label
  static const char event_groupname[] = "custom_events";
  if (watcher->sample_type_id == DDPROF_PWT_TRACEPOINT) {
    watcher->tracepoint_name = watcher->desc;
    watcher->tracepoint_group = event_groupname;
  }
  return true;
}

#define R(x) REGNAME(x)
#ifdef __x86_64__
int arg2reg[] = {-1, R(RDI), R(RSI), R(RDX), R(RCX), R(R8), R(R9)};
#elif __aarch64__
int arg2reg[] = {-1, R(X0), R(X1), R(X2), R(X3), R(X4), R(X5), R(X6)};
#else
#  error Your architecture is not supported
#endif
#undef R
uint8_t get_register(const char *str) {
  uint8_t reg = 0;
  char *str_copy = (char *)str;
  long reg_buf = strtol(str, &str_copy, 10);
  if (!*str_copy) {
    reg = reg_buf;
  } else {
    reg = 0;
    LG_NTC("Could not parse register %s", str);
  }

  // If we're here, then we have a register.
  return arg2reg[reg];
}

bool get_trace_format(const char *str, uint8_t *trace_off, uint8_t *trace_sz) {
  char *str_copy = (char *)str;
  if (!str)
    return false;

  char *period = strchr(str, '.');
  char *period_copy = period;
  if (!period)
    return false;

  *trace_off = strtol(str, &str_copy, 10);
  *trace_sz = strtol(period + 1, &period_copy, 10);

  // Error if the size is zero, otherwise fine probably
  return !trace_sz;
}

int tracepoint_id_from_event(const char *eventname, const char *groupname) {
  if (!event || !*event || !group || !*group)
    return -1;

  static char path[4096]; // Arbitrary, but path sizes limits are difficult
  static char buf[64];    // For reading
  char *buf_copy = buf;
  int pathsz =
      snprintf(path, sizeof(path), "/sys/kernel/tracing/events/%s/%s/id",
               groupname, eventname);
  if (pathsz >= sizeof(path)) {
    // Possibly ran out of room
    free(str);
    return -1;
  }
  int fd = open(path, O_RDONLY);
  if (-1 == fd) {
    free(str);
    return -1;
  }

  // Read the data in an eintr-safe way
  int read_ret = -1;
  long trace_id = 0;
  do {
    read_ret = read(fd, buf, sizeof(buf));
  } while (read_ret == -1 && errno == EINTR);
  close(fd);
  if (read_ret > 0)
    trace_id = strtol(buf, &buf_copy, 10);
  if (*buf_copy && *buf_copy != '\n') {
    free(str);
    return false;
  }

  return trace_id;
}

// If this returns false, then the passed watcher should be regarded as invalid
bool watcher_from_tracepoint(const char *str, PerfWatcher *watcher) {

  // OK done
  *watcher = *twatcher_default();
  watcher->config = trace_id;
  watcher->sample_period = period;
  if (is_raw)
    watcher->sample_type |= PERF_SAMPLE_RAW;
  if (reg) {
    watcher->reg = reg;
  } else {
    watcher->trace_off = trace_off;
    watcher->trace_sz = trace_sz;
  }
  watcher->tracepoint_group = groupname;
  watcher->tracepoint_name = tracename;
  return true;
}
