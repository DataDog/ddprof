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

DDRes add_preset(DDProfContext *ctx, const char *preset,
                 bool pid_or_global_mode) {
  using namespace std::literals;
  static Preset presets[] = {
      {"default", {"sCPU", "sALLOC"}},
      {"default-pid", {"sCPU"}},
      {"cpu_only", {"sCPU"}},
      {"alloc_only", {"sALLOC"}},
      {"cpu_live_heap", {"sCPU", "sALLOC mode=l"}},
  };

  if (preset == "default"sv && pid_or_global_mode) {
    preset = "default-pid";
  }

  ddprof::span presets_span{presets};
  std::string_view preset_sv{preset};

  auto it = std::find_if(presets_span.begin(), presets_span.end(),
                         [&preset_sv](auto &e) { return e.name == preset_sv; });
  if (it == presets_span.end()) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_INPUT_PROCESS, "Unknown preset (%s)",
                           preset);
  }

  for (const char *event : it->events) {
    if (event == nullptr) {
      break;
    }
    if (ctx->num_watchers == MAX_TYPE_WATCHER) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_INPUT_PROCESS, "Too many input events");
    }
    PerfWatcher *watcher = &ctx->watchers[ctx->num_watchers];
    if (!watcher_from_str(event, watcher)) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_INPUT_PROCESS,
                             "Invalid event/tracepoint (%s)", event);
    }
    ddprof::span watchers{ctx->watchers,
                          static_cast<size_t>(ctx->num_watchers)};

    // ignore event if it was already present in watchers
    if (watcher->ddprof_event_type == DDPROF_PWE_TRACEPOINT ||
        std::find_if(watchers.begin(), watchers.end(), [&watcher](auto &w) {
          return w.ddprof_event_type == watcher->ddprof_event_type;
        }) == watchers.end()) {
      ++ctx->num_watchers;
    }
  }

  return {};
}
} // namespace ddprof
