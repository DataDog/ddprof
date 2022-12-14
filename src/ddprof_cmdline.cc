// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_cmdline.hpp"

#include <assert.h>
#include <cstring>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "ddres_helpers.hpp"
#include "event_config.hpp"
#include "event_parser.h"
#include "perf_archmap.hpp"
#include "perf_watcher.hpp"

int arg_which(const char *str, const std::vector<std::string_view> &list) {
  size_t sz = strlen(str);
  for (size_t i = 0; i < list.size(); ++i) {
    const auto &el = list[i];
    if (sz == el.size() && !strcasecmp(el.data(), str))
      return static_cast<int>(i);
  }
  return -1;
}

int arg_which(const char *str, char const *const *set, int sz_set) {
  if (!str || !set)
    return -1;
  for (int i = 0; i < sz_set; i++) {
    if (set[i] && !strcasecmp(str, set[i]))
      return i;
  }
  return -1;
}

bool arg_inlist(const char *str, const std::vector<std::string_view> &list) {
  return -1 != arg_which(str, list);
}

bool arg_inset(const char *str, char const *const *set, int sz_set) {
  int ret = arg_which(str, set, sz_set);
  return !(-1 == ret);
}

bool is_yes(const char *str) {
  static std::vector<std::string_view> list = {"1", "on", "yes", "true", "enable", "enabled"};
  return arg_inlist(str, list);
}

bool is_yes(const std::string &str) {
  return is_yes(str.c_str());
}

bool is_no(const char *str) {
  static std::vector<std::string_view> list = {"0", "off", "no", "false", "disable", "disabled"};
  return arg_inlist(str, list);
}

bool is_no(const std::string &str) {
  return is_no(str.c_str());
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
    *watcher = *tracepoint_default_watcher();
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

  // The output mode isn't set as part of the configuration templates; we
  // always default to callgraph mode
  watcher->output_mode = EventConfMode::kCallgraph;
  if (EventConfMode::kAll <= conf->mode) {
    watcher->output_mode = conf->mode;
  }

  // Configure some stuff relative to the mode
  if (!(EventConfMode::kCallgraph <= watcher->output_mode)) {
    watcher->sample_type &= ~PERF_SAMPLE_STACK_USER;
    watcher->sample_stack_size = 0;
  }

  // Configure the sampling strategy.  If no valid conf, use template default
  if (conf->cadence != 0) {
    if (conf->cad_type == EventConfCadenceType::kPeriod) {
      watcher->sample_period = conf->cadence;
      watcher->options.is_freq = false;
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

  watcher->tracepoint_event = conf->eventname;
  watcher->tracepoint_group = conf->groupname;
  watcher->tracepoint_label = conf->label;
  return true;
}
