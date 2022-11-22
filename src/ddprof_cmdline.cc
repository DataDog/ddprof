// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_cmdline.hpp"

#include <assert.h>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ddres_helpers.hpp"
#include "perf_archmap.hpp"
#include "perf_watcher.hpp"

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

long id_from_tracepoint(const char *gname, const char *tname) {
  char path[2048] = {0}; // somewhat arbitrarily
  size_t sz_path = sizeof(path);
  char buf[64] = {0};
  char *buf_copy = buf;

  // Need to figure out whether we use debugfs or tracefs
  static int use_tracefs = -1; // -2 error, -1 init, 0 no, 1 yes
  static char tracefs_path[] = "/sys/kernel/tracing/events";
  static char debugfs_path[] = "/sys/kernel/debug/tracing/events";

  if (!gname || !*gname || !tname || !*tname) {
    return -1;
  }

  if (use_tracefs == -2) {
    // We checked in a previous loop and couldn't read tracef or debugfs
    return -1;
  } else if (use_tracefs == -1) {
    struct stat sb;
    if (stat(tracefs_path, &sb)) {
      // If we're here, the stat failed so we can't use tracefs
      if (stat(debugfs_path, &sb)) {
        // If we're here, debugfs failed too, return error
        use_tracefs = -2;
        return -1;
      }
      use_tracefs = 0; // Use debugfs
    } else {
      use_tracefs = 1; // Use tracefs
    }
  }

  // Check validity of given tracepoint
  char *spath = use_tracefs ? tracefs_path : debugfs_path;
  int pathsz = snprintf(path, sz_path, "%s/%s/%s/id", spath, gname, tname);
  if (static_cast<size_t>(pathsz) >= sz_path) {
    // Possibly ran out of room
    return -1;
  }
  int fd = open(path, O_RDONLY);
  if (-1 == fd) {
    return -1;
  }

  // Read the data in an eintr-safe way
  int read_ret = -1;
  long trace_id = -1;
  do {
    read_ret = read(fd, buf, sizeof(buf));
  } while (read_ret == -1 && errno == EINTR);
  close(fd);
  if (read_ret > 0)
    trace_id = strtol(buf, &buf_copy, 10);
  if (*buf_copy && *buf_copy != '\n') {
    return -1;
  }

  return trace_id;
}

// If this returns false, then the passed watcher should be regarded as invalid
bool watcher_from_event(const char *str, PerfWatcher *watcher) {
  const PerfWatcher *tmp_watcher;
  if (!(tmp_watcher = ewatcher_from_str(str)))
    return false;

  // Now we have to process options out of the string
  char *str_chk; // for checking the result of parsing
  const char *str_tmp = std::strchr(str, ','); // points to ',' or is nullptr
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
  // Certain watcher configs get additional event information
  if (watcher->config == kDDPROF_COUNT_ALLOCATIONS) {
    watcher->sample_type |= PERF_SAMPLE_ADDR;
  }

  // Some profiling types get lots of additional state transplanted here
  if (watcher->options.is_overloaded) {
    if (watcher->ddprof_event_type == DDPROF_PWE_tALLOCSYS1) {
      // tALLOCSY1 overrides perfopen to bind together many file descriptors
      watcher->instrument_self = true;
      watcher->options.is_kernel = kPerfWatcher_Try;
      watcher->sample_stack_size /= 2; // Make this one smaller than normal
      watcher->sample_type |= PERF_SAMPLE_RAW;
      watcher->options.is_kernel = kPerfWatcher_Try;
    } else if (watcher->ddprof_event_type == DDPROF_PWE_tALLOCSYS2) {
      // tALLOCSYS2 captures all syscalls; used to troubleshoot 1
      watcher->tracepoint_group = "raw_syscalls";
      watcher->tracepoint_name = "sys_exit";
      long id = id_from_tracepoint("raw_syscalls", "sys_exit");
      if (-1 == id) {
        // We mutated the user's event, but it is invalid.
        return false;
      }
      watcher->config = id;
      watcher->sample_type |= PERF_SAMPLE_RAW;
      watcher->options.is_kernel = kPerfWatcher_Try;
    } else if (watcher->ddprof_event_type == DDPROF_PWE_tOPENFD) {
      watcher->instrument_self = true;
      watcher->sample_type |= PERF_SAMPLE_RAW;
      watcher->options.is_kernel = kPerfWatcher_Try;
    } else if (watcher->ddprof_event_type == DDPROF_PWE_tNOISYCPU1) {
      PRINT_NFO("Disabling call stacks for noisy neighbors1");
      PRINT_NFO("DISABLING SELF-INSTRUMENT for noisy neighbors1");
      watcher->sample_stack_size = 0; // Disable for now
      watcher->sample_type |= PERF_SAMPLE_CPU | PERF_SAMPLE_IDENTIFIER;
      watcher->tracepoint_name = "sCPU";
    } else if (watcher->ddprof_event_type == DDPROF_PWE_tNOISYCPU2) {
      PRINT_NFO("Disabling call stacks for noisy neighbors");
      watcher->sample_stack_size = 0; // Disable for now
      watcher->instrument_self = true;
      watcher->sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_ID | PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD | PERF_SAMPLE_RAW | PERF_SAMPLE_IDENTIFIER;
      watcher->options.is_kernel = kPerfWatcher_Try;
    }
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
  if (!str)
    return false;

  const char *period = std::strchr(str, '.');
  if (!period)
    return false;

  *trace_off = strtol(str, nullptr, 10);
  *trace_sz = strtol(period + 1, nullptr, 10);

  // Error if the size is zero, otherwise fine probably
  return !trace_sz;
}

// If this returns false, then the passed watcher should be regarded as invalid
bool watcher_from_tracepoint(const char *_str, PerfWatcher *watcher) {
  // minimum form; provides counts, samples every hit
  // -t groupname:tracename
  // Register-qualified form
  // -t groupname:tracename%REG
  // -t groupname:tracename$offset.size
  // Sample-qualified form, sets a period value
  // -t groupname:tracename@period
  // full
  // -t groupename:tracename%REG@period
  // groupname, tracename, REG - strings
  // REG - can be a number 1-6
  // period is a number
  char *str = strdup(_str);
  size_t sz_str = strlen(str);
  const char *gname;
  const char *tname;
  uint8_t reg = 0;
  uint64_t period = 1;
  bool is_raw = false;
  uint8_t trace_off = 0;
  uint8_t trace_sz = 0;

  // Check format
  if (!sz_str) {
    free(str);
    return false;
  }
  char *colon = strchr(str, ':');
  char *perc = strchr(str, '%');
  char *amp = strchr(str, '@');
  char *dollar = strchr(str, '$');

  if (!colon || (dollar && perc)) {
    free(str);
    return false;
  }

  // Split strings
  *colon = 0; // colon is true from previous check
  if (perc)
    *perc = 0;
  if (amp)
    *amp = 0;
  if (dollar)
    *dollar = 0;

  // Name checking
  gname = str;
  tname = colon + 1;

  // If a register is specified, process that
  if (perc)
    reg = get_register(perc + 1);

  // Handle raw event parameters
  if (dollar && !get_trace_format(dollar + 1, &trace_off, &trace_sz)) {
    is_raw = true;
  } else {
    trace_off = 0;
    trace_sz = 0;
  }

  // If the user specified a period, make sure it is valid
  if (amp) {
    char *str_check = (char *)str;
    uint64_t buf = strtoll(amp + 1, &str_check, 10);
    if (!*str_check)
      period = buf;
  }

  // perf_event needs the actual ID of the tracepoint
  long id = id_from_tracepoint(gname, tname);
  if (id == -1) {
    free(str);
    return false;
  }

  // OK done
  *watcher = *twatcher_default();
  watcher->config = id;
  watcher->sample_period = period;
  if (is_raw) {
    watcher->sample_type |= PERF_SAMPLE_RAW;
  }
  if (reg) {
    watcher->reg = reg;
  } else {
    watcher->trace_off = trace_off;
    watcher->trace_sz = trace_sz;
  }
  watcher->tracepoint_group = gname;
  watcher->tracepoint_name = tname;
  return true;
}
