// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "presets.hpp"

#include "ddres.hpp"

#include "ddprof_cmdline.hpp"
#include "span.hpp"

#include <algorithm>
#include <string_view>

namespace ddprof {

DDRes add_preset(std::string_view preset, bool pid_or_global_mode,
                 uint32_t default_sample_stack_user,
                 std::vector<PerfWatcher> &watchers) {
  using namespace std::literals;
  static Preset presets[] = {
      {"default", "sCPU;sALLOC"},
      {"default-pid", "sCPU"},
      {"cpu_only", "sCPU"},
      {"alloc_only", "sALLOC"},
      {"cpu_live_heap", "sCPU;sALLOC mode=l"},
  };

  if (preset == "default"sv && pid_or_global_mode) {
    preset = "default-pid"sv;
  }
  ddprof::span presets_span{presets};
  auto it = std::find_if(presets_span.begin(), presets_span.end(),
                         [&preset](auto &e) { return e.name == preset; });
  if (it == presets_span.end()) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_INPUT_PROCESS, "Unknown preset (%.*s)",
                           static_cast<int>(preset.size()), preset.data());
  }

  std::vector<PerfWatcher> new_watchers;
  if (!watchers_from_str(it->events, new_watchers, default_sample_stack_user)) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_INPUT_PROCESS,
                           "Invalid event/tracepoint (%s)", it->events);
  }

  for (auto &watcher : new_watchers) {
    // todo: missing management of several modes for live heap
    // ignore event if it was already present in watchers
    if (watcher.ddprof_event_type == DDPROF_PWE_TRACEPOINT ||
        std::find_if(watchers.begin(), watchers.end(), [&watcher](auto &w) {
          return w.ddprof_event_type == watcher.ddprof_event_type;
        }) == watchers.end()) {
      watchers.push_back(std::move(watcher));
    }
  }
  return {};
}

} // namespace ddprof
