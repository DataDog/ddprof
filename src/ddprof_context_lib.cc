// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_context_lib.hpp"

#include "ddprof_cli.hpp"
#include "ddprof_cmdline.hpp"
#include "ddprof_context.hpp"
#include "ddprof_cpumask.hpp"
#include "logger.hpp"
#include "logger_setup.hpp"
#include "presets.hpp"
#include "span.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <unistd.h>

namespace ddprof {
static const PerfWatcher *
find_duplicate_event(ddprof::span<const PerfWatcher> watchers) {
  bool seen[DDPROF_PWE_LENGTH] = {};
  for (auto &watcher : watchers) {
    if (watcher.ddprof_event_type != DDPROF_PWE_TRACEPOINT &&
        seen[watcher.ddprof_event_type]) {
      return &watcher;
    }
    seen[watcher.ddprof_event_type] = true;
  }
  return nullptr;
}

static void order_watchers(ddprof::span<PerfWatcher> watchers) {
  // Ensure that non-perf watchers are last because they might depend on
  // processing perf events before (comm, mmap, ...)
  std::stable_sort(
      watchers.begin(), watchers.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.type < PERF_TYPE_MAX && rhs.type >= PERF_TYPE_MAX;
      });
}

namespace {
void copy_cli_values(const DDProfCLI &ddprof_cli, DDProfContext &ctx) {

  // Do we want to std::move more ?
  ctx.exp_input = ddprof_cli.exporter_input;
  // todo avoid manual copies
  ctx.params.tags = ddprof_cli.tags;
  // Profiling settings
  if (ddprof_cli.global) {
    // global mode is flagged as pid == -1
    ctx.params.pid = -1;
  } else {
    ctx.params.pid = ddprof_cli.pid;
  }
  ctx.params.upload_period = ddprof_cli.upload_period;
  // todo : naming ?
  ctx.params.worker_period = ddprof_cli.worker_period;
  // Advanced
  ctx.params.switch_user = ddprof_cli.switch_user;
  ctx.params.nice = ddprof_cli.nice;
  // Debug
  ctx.params.internal_stats = ddprof_cli.internal_stats;
  ctx.params.enable = ddprof_cli.enable;
  // Extended
  if (!ddprof_cli.cpu_affinity.empty() &&
      !parse_cpu_mask(ddprof_cli.cpu_affinity, ctx.params.cpu_affinity)) {
    LG_WRN("Unable to parse cpu_affinity setting");
  }

  ctx.params.show_samples = ddprof_cli.show_samples;
  ctx.params.fault_info = ddprof_cli.fault_info;

  ctx.params.sockfd = ddprof_cli.socket;
  if (ctx.params.sockfd != -1) {
    ctx.params.wait_on_socket = true;
  }
}

DDRes context_add_watchers(const DDProfCLI &ddprof_cli, DDProfContext &ctx) {
  std::vector<PerfWatcher> watchers;
  DDRES_CHECK_FWD(ddprof_cli.add_watchers_from_events(watchers));
  if (const PerfWatcher *dup_watcher = find_duplicate_event(watchers);
      dup_watcher != nullptr) {
    DDRES_RETURN_ERROR_LOG(
        DD_WHAT_INPUT_PROCESS, "Duplicate event found in input: %s",
        event_type_name_from_idx(dup_watcher->ddprof_event_type));
  }

  std::string preset = ddprof_cli.preset;

  if (preset.empty() && watchers.size() == 0) {
    // use `default` preset when no preset and no events were given in input
    preset = "default";
  }

  if (!preset.empty()) {
    bool pid_or_global_mode =
        (ddprof_cli.global || ddprof_cli.pid) && ctx.params.sockfd == -1;
    DDRES_CHECK_FWD(add_preset(preset, pid_or_global_mode,
                               ddprof_cli.default_sample_stack_user, watchers));
  }

  // Add a dummy watcher if needed
  if (std::find_if(watchers.begin(), watchers.end(), [](const auto &watcher) {
        return watcher.type < PERF_TYPE_MAX;
      }) == watchers.end()) {
    // without a perf watcher we need a dummy watcher to grab mmap events
    watchers.push_back(*ewatcher_from_str("sDUM"));
  }

  order_watchers(watchers);

  ctx.watchers = std::move(watchers);
  return {};
}
} // namespace

DDRes context_set(const DDProfCLI &ddprof_cli, DDProfContext &ctx) {
  setup_logger(ddprof_cli.log_mode.c_str(), ddprof_cli.log_level.c_str());

  copy_cli_values(ddprof_cli, ctx);

  ctx.params.num_cpu = ddprof::nprocessors_conf();

  DDRES_CHECK_FWD(context_add_watchers(ddprof_cli, ctx));

  if (ddprof_cli.show_config) {
    ddprof_cli.print();
    PRINT_NFO("Instrumented with %lu watchers:", ctx.watchers.size());
    for (unsigned i = 0; i < ctx.watchers.size(); ++i) {
      log_watcher(&(ctx.watchers[i]), i);
    }
  }

  return {};
}

int context_allocation_profiling_watcher_idx(const DDProfContext &ctx) {
  ddprof::span watchers{ctx.watchers};
  auto it =
      std::find_if(watchers.begin(), watchers.end(), [](const auto &watcher) {
        return watcher.type == kDDPROF_TYPE_CUSTOM &&
            watcher.config == kDDPROF_COUNT_ALLOCATIONS;
      });

  if (it != watchers.end()) {
    return it - watchers.begin();
  }
  return -1;
}

} // namespace ddprof
