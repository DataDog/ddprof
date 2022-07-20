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
#include "event_config.h"
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
  return true;
}

int tracepoint_id_from_event(const char *eventname, const char *groupname) {
  if (!eventname || !*eventname || !groupname || !*groupname)
    return -1;

  static char path[4096]; // Arbitrary, but path sizes limits are difficult
  static char buf[64];    // For reading
  char *buf_copy = buf;
  size_t pathsz =
      snprintf(path, sizeof(path), "/sys/kernel/tracing/events/%s/%s/id",
               groupname, eventname);
  if (pathsz >= sizeof(path))
    return -1;
  int fd = open(path, O_RDONLY);
  if (-1 == fd)
    return -1;

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
    return false;

  return trace_id;
}

// If this returns false, then the passed watcher should be regarded as invalid
bool watcher_from_tracepoint(const char *str, PerfWatcher *watcher) {
  EventConf *conf = EventConf_parse(str);
  if (!conf)
    return false;

  // Start out with the correct template.
  *watcher = *twatcher_default();

  // The most likely thing to be invalid is the selection of the tracepoint
  // from the trace events system.  If the conf has a nonzero number for the id
  // we assume the user has privileged information and knows what they want.
  // Else, we use the group/event combination to extract that id from the
  // tracefs filesystem in the canonical way.
  int tracepoint_id = -1;
  if (conf->id > 0) {
    tracepoint_id = conf->id;
  } else {
    tracepoint_id = tracepoint_id_from_event(conf->eventname, conf->groupname);
  }

  if (tracepoint_id == -1) {
    return false;
  }
  watcher->config = tracepoint_id;

  // Configure the sampling strategy.  If no valid conf, use template default
  if (conf->cad_type == ECCAD_PERIOD && conf->cadence > 0) {
    watcher->sample_period = conf->cadence;
  } else if (conf->cad_type == ECCAD_FREQ && conf->cadence > 0) {
    watcher->sample_frequency = conf->cadence;
    watcher->options.is_freq = true;
  }

  // Configure the data source
  if (conf->loc_type == ECLOC_RAW) {
    watcher->sample_type |= PERF_SAMPLE_RAW;
    watcher->trace_off = conf->arg_offset;
    if (conf->arg_size > 0)
      watcher->trace_sz = conf->arg_size;
    else
      watcher->trace_sz = sizeof(uint64_t); // default raw entry
  } else if (conf->loc_type == ECLOC_REG) {
    watcher->reg = conf->register_num;
  }

  if (conf->arg_coeff != 0.0)
    watcher->value_coefficient = conf->arg_coeff;

  watcher->tracepoint_group = conf->groupname;
  watcher->tracepoint_name = conf->eventname;
  return true;
}
