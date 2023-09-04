// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf_watcher.hpp"

#include "logger.hpp"
#include "perf.hpp"

#include <stddef.h>
#include <string.h>

#define BASE_STYPES                                                            \
  (PERF_SAMPLE_STACK_USER | PERF_SAMPLE_REGS_USER | PERF_SAMPLE_TID |          \
   PERF_SAMPLE_TIME | PERF_SAMPLE_PERIOD)

uint64_t perf_event_default_sample_type() { return BASE_STYPES; }

#define X_STR(a, b, c, d) {b,d},
const char *sample_type_name_from_idx(int idx, bool live) {
  static const std::array<std::pair<const char *,const char *>>sample_names
      = {PROFILE_TYPE_TABLE(X_STR)};
  if (idx < 0 || idx >= DDPROF_PWT_LENGTH)
    return NULL;
  if (live) {
    return sample_names[idx].second;
  }
  return sample_names[idx].first;
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
  {DDPROF_PWE_##a, b, BASE_STYPES, c, d, {e}, f, g},

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

void log_watcher(const PerfWatcher *w, int idx) {
  PRINT_NFO("  - ID: %s, Pos: %d, Index: %lu", w->desc.c_str(), idx, w->config);
  switch (w->value_source) {
  case EventConfValueSource::kSample:
    PRINT_NFO("    Location: Sample");
    break;
  case EventConfValueSource::kRegister:
    PRINT_NFO("    Location: Register, regno: %d", w->regno);
    break;
  case EventConfValueSource::kRaw:
    PRINT_NFO("    Location: Raw event, offset: %d, size: %d", w->raw_off,
              w->raw_sz);
    break;
  default:
    PRINT_NFO("    ILLEGAL LOCATION");
    break;
  }

  PRINT_NFO("    Category: %s, EventName: %s, GroupName: %s, Label: %s",
            sample_type_name_from_idx(w->sample_type_id),
            w->tracepoint_event.c_str(), w->tracepoint_group.c_str(),
            w->tracepoint_label.c_str());
  PRINT_NFO("    Sample user Stack Size: %u", w->options.stack_sample_size);

  if (w->options.is_freq)
    PRINT_NFO("    Cadence: Freq, Freq: %lu", w->sample_frequency);
  else
    PRINT_NFO("    Cadence: Period, Period: %ld", w->sample_period);
  if (Any(EventConfMode::kCallgraph & w->output_mode))
    PRINT_NFO("    Outputting to callgraph (flamegraph)");
  if (Any(EventConfMode::kMetric & w->output_mode))
    PRINT_NFO("    Outputting to metric");
  if (Any(EventConfMode::kLiveCallgraph & w->output_mode))
    PRINT_NFO("    Outputting to live callgraph");
}

std::string_view watcher_help_text() {
  static const std::string help_text =
      // clang-format off
      "\nEvent Configuration Documentation\n"
"===================================\n"
"Events define " + std::string(MYNAME) + "'s instrumentation settings.\n\n"
"General Syntax for Event Configuration:\n"
"---------------------------------------\n"
"Events are defined by their type and associated key value settings:\n\n"
"<type Of event> <key1>:<value1>\n"
"Or using comma as a separator: \n"
"<type Of event>,<key1>:<value1>\n"
"Events are repeatable\n\n"
"Common Examples:\n"
"----------------\n"
"1. CPU profiling with a custom sampling frequency: -e \"sCPU p=50\"\n"
"2. Live Allocation Tracking (leak detection):\n"
"  -e sALLOC,mode=l\n\n"
"Event Types:\n"
"------------\n"
"The most common types are:\n"
"- sCPU for CPU Time \n"
"- sALLOC for allocations (only available in wrapper mode) \n"
"Please consult the `https://github.com/DataDog/ddprof/blob/main/include/perf_watcher.hpp#L117-L138` for an up to date list of available events. \n"
"Note: Some events may require hardware support and elevated permissions.\n\n"
"Configuration Keys:\n"
"-------------------\n"
"- `s|value_scale|scale`: Scaling factor for the event.\n"
"- `f|frequency|freq`: Frequency at which the event occurs.\n"
"- `e|event|eventname|ev`: Name of the event.\n"
"- `g|group|groupname|gr`: Name of the group to which the event belongs.\n"
"- `i|id`: Identifier for the event.\n"
"- `l|label`: Label for the event.\n"
"- `m|mode`: Mode of the event.\n"
"- `n|arg_num|argno`: Argument number to retrieve a value associated with this event.\n"
"- `p|period|per`: Period of the event.\n"
"- `r|register|regno`: Register to retrieve the value associated with this event.\n"
"- `st|stack_sample_size|stcksz : Same as the stack_sample_size input option for this event."
"- `o|raw_offset|rawoff`: Raw offset to retrieve the value associated with this event.\n"
"- `z|raw_size|rawsz`: Raw size associated to raw offset.\n\n"
"Disclaimer:\n"
"-----------\n"
"Please note that this documentation is currently under construction. We recommend the use of presets.\n"
"Not all options may be fully supported within the Datadog UI at present, and the described grammar is subject to change.\n"
"Exercise caution and double-check your configurations before implementation.\n";
  // clang-format on
  return help_text;
}
