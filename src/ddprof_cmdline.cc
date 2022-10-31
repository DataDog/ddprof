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
#include "event_config.hpp"
#include "event_parser.h"
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

unsigned int tracepoint_id_from_event(const char *eventname,
                                      const char *groupname) {
  if (!eventname || !*eventname || !groupname || !*groupname)
    return 0;

  static char path[4096]; // Arbitrary, but path sizes limits are difficult
  static char buf[sizeof("4294967296")]; // For reading 32-bit decimal int
  char *buf_copy = buf;
  size_t pathsz =
      snprintf(path, sizeof(path), "/sys/kernel/tracing/events/%s/%s/id",
               groupname, eventname);
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

// If this returns false, then the passed watcher should be regarded as invalid
constexpr uint64_t kIgnoredWatcherID = -1ul;
bool watcher_from_str(const char *str, PerfWatcher *watcher) {
  EventConf *conf = EventConf_parse(str);
  if (!conf) {
    return false;
  }

  // If there's no eventname, then this configuration is invalid
  if (conf->eventname.empty()) {
    return false;
  }

  // The watcher is templated; either from an existing Profiling template,
  // keyed on the eventname, or it uses the generic template for Tracepoints
  const PerfWatcher *tmp_watcher = ewatcher_from_str(conf->eventname.c_str());
  if (tmp_watcher) {
    *watcher = *tmp_watcher;
    conf->id = kIgnoredWatcherID; // matched, so invalidate Tracepoint checks
  } else if (!conf->groupname.empty()) {
    // If the event doesn't match an ewatcher, it is only valid if a group was
    // also provided (splitting events on ':' is the responsibility of the
    // parser)
    auto *tmp_tracepoint_watcher = tracepoint_default_watcher();
    if (!tmp_tracepoint_watcher) {
      return false;
    }
    *watcher = *tmp_tracepoint_watcher;
  } else {
    return false;
  }

  // The most likely thing to be invalid is the selection of the tracepoint
  // from the trace events system.  If the conf has a nonzero number for the id
  // we assume the user has privileged information and knows what they want.
  // Else, we use the group/event combination to extract that id from the
  // tracefs filesystem in the canonical way.
  uint64_t tracepoint_id = 0;
  if (conf->id > 0) {
    tracepoint_id = conf->id;
  } else {
    tracepoint_id = tracepoint_id_from_event(conf->eventname.c_str(),
                                             conf->groupname.c_str());
  }

  // 0 is an error, "-1" is ignored
  if (!tracepoint_id) {
    return false;
  } else if (tracepoint_id != kIgnoredWatcherID) {
    watcher->config = tracepoint_id;
  }

  // Configure the sampling strategy.  If no valid conf, use template default
  if (conf->cadence != 0) {
    if (conf->cad_type == EventConfCadenceType::kPeriod) {
      watcher->sample_period = conf->cadence;
    } else if (conf->cad_type == EventConfCadenceType::kFrequency) {
      watcher->sample_frequency = conf->cadence;
      watcher->options.is_freq = true;
    }
  }

  // Configure value source
  if (conf->value_source == EventConfValueSource::kRaw) {
    watcher->value_source = EventConfValueSource::kRaw;
    watcher->sample_type |= PERF_SAMPLE_RAW;
    watcher->raw_off = conf->raw_offset;
    if (conf->raw_size > 0)
      watcher->raw_sz = conf->raw_size;
    else
      watcher->raw_sz = sizeof(uint64_t); // default raw entry
  } else if (conf->value_source == EventConfValueSource::kRegister) {
    watcher->regno = conf->register_num;
    watcher->value_source = EventConfValueSource::kRegister;
  }

  if (conf->value_scale != 0.0)
    watcher->value_scale = conf->value_scale;

  // The output mode isn't set as part of the configuration templates; we
  // always default to callgraph mode
  watcher->output_mode = EventConfMode::kCallgraph;
  if (EventConfMode::kAll <= conf->mode) {
    watcher->output_mode = conf->mode;
  }

  watcher->tracepoint_event = conf->eventname;
  watcher->tracepoint_group = conf->groupname;
  watcher->tracepoint_label = conf->label;

  // Certain watcher configs get additional event information
  if (watcher->config == kDDPROF_COUNT_ALLOCATIONS) {
    watcher->sample_type |= PERF_SAMPLE_ADDR;
  }

  // Some profiling types get lots of additional state transplanted here
  if (watcher->options.is_overloaded) {
    if (watcher->ddprof_event_type == DDPROF_PWE_tALLOCSYS1) {
      // tALLOCSY1 overrides perfopen to bind together many file descriptors
      watcher->tracepoint_group = "syscalls";
      watcher->tracepoint_label = "sys_exit_mmap";
      watcher->instrument_self = true;
      watcher->options.use_kernel = PerfWatcherUseKernel::kTry;
      watcher->sample_stack_size /= 2; // Make this one smaller than normal

    } else if (watcher->ddprof_event_type == DDPROF_PWE_tALLOCSYS2) {
      // tALLOCSYS2 captures all syscalls; used to troubleshoot 1
      watcher->tracepoint_group = "raw_syscalls";
      watcher->tracepoint_label = "sys_exit";
      long id = id_from_tracepoint("raw_syscalls", "sys_exit");
      if (-1 == id) {
        // We mutated the user's event, but it is invalid.
        return false;
      }
      watcher->config = id;
    }
    watcher->sample_type |= PERF_SAMPLE_RAW;
    watcher->options.use_kernel = PerfWatcherUseKernel::kTry;
  }

  return true;
}
