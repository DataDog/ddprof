// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_context_lib.hpp"

#include "ddprof_cli.hpp"
#include "ddprof_cmdline.hpp"
#include "ddprof_context.hpp"
#include "ddprof_cpumask.hpp"
#include "ddres.hpp"
#include "ipc.hpp"
#include "logger.hpp"
#include "logger_setup.hpp"
#include "presets.hpp"
#include "prng.hpp"
#include "sdt_probe.hpp"
#include "uprobe_attacher.hpp"

#include <algorithm>
#include <charconv>
#include <span>
#include <string_view>
#include <unistd.h>

namespace ddprof {

namespace {

// Generate a pseudo-random unique socket path
std::string generate_socket_path() {
  char path[PATH_MAX];
  static xoshiro256ss engine{std::random_device{}()};
  constexpr auto kSuffixLen = 8;
  auto random_suffix = generate_random_string(engine, kSuffixLen);
  snprintf(path, sizeof(path), "@/tmp/ddprof-%d-%s.sock", getpid(),
           random_suffix.c_str());
  return path;
}

const PerfWatcher *find_duplicate_event(std::span<const PerfWatcher> watchers) {
  bool seen[DDPROF_PWE_LENGTH] = {};
  for (const auto &watcher : watchers) {
    if (watcher.ddprof_event_type != DDPROF_PWE_TRACEPOINT &&
        seen[watcher.ddprof_event_type]) {
      return &watcher;
    }
    seen[watcher.ddprof_event_type] = true;
  }
  return nullptr;
}

void order_watchers(std::span<PerfWatcher> watchers) {
  // Ensure that non-perf watchers are last because they might depend on
  // processing perf events before (comm, mmap, ...)
  std::stable_sort(
      watchers.begin(), watchers.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.type < PERF_TYPE_MAX && rhs.type >= PERF_TYPE_MAX;
      });
}

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
  ctx.params.inlined_functions = ddprof_cli.inlined_functions;
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
  ctx.params.timeline = ddprof_cli.timeline;
  ctx.params.fault_info = ddprof_cli.fault_info;
  ctx.params.remote_symbolization = ddprof_cli.remote_symbolization;
  ctx.params.disable_symbolization = ddprof_cli.disable_symbolization;
  ctx.params.reorder_events = ddprof_cli.reorder_events;
  ctx.params.maximum_pids = ddprof_cli.maximum_pids;

  ctx.params.initial_loaded_libs_check_delay =
      ddprof_cli.initial_loaded_libs_check_delay;
  ctx.params.loaded_libs_check_interval = ddprof_cli.loaded_libs_check_interval;
  ctx.params.socket_path = ddprof_cli.socket_path;
  ctx.params.pipefd_to_library = UniqueFd{ddprof_cli.pipefd_to_library};

  // SDT probe options
  ctx.params.sdt_mode = ddprof_cli.sdt_mode;
  ctx.params.target_binary = ddprof_cli.target_binary;
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

  if (preset.empty() && watchers.empty()) {
    // use `default` preset when no preset and no events were given in input
    preset = "default";
  }

  if (!preset.empty()) {
    const bool pid_or_global_mode =
        (ddprof_cli.global || ddprof_cli.pid) && !ctx.params.pipefd_to_library;
    DDRES_CHECK_FWD(add_preset(preset, pid_or_global_mode,
                               ddprof_cli.default_stack_sample_size, watchers));
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
  setup_logger(ddprof_cli.log_mode.c_str(), ddprof_cli.log_level.c_str(),
               MYNAME);

  copy_cli_values(ddprof_cli, ctx);

  ctx.params.num_cpu = nprocessors_conf();

  DDRES_CHECK_FWD(context_add_watchers(ddprof_cli, ctx));

  // Setup SDT probes if applicable
  DDRES_CHECK_FWD(context_setup_sdt_probes(ddprof_cli, ctx));

  if (ctx.params.socket_path.empty()) {
    ctx.params.socket_path = generate_socket_path();
  }

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
  const std::span watchers{ctx.watchers};
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

int context_sdt_allocation_profiling_watcher_idx(const DDProfContext &ctx) {
  const std::span watchers{ctx.watchers};
  auto it =
      std::find_if(watchers.begin(), watchers.end(), [](const auto &watcher) {
        return watcher.type == kDDPROF_TYPE_SDT_UPROBE &&
            watcher.config == kDDPROF_COUNT_ALLOCATIONS_SDT;
      });

  if (it != watchers.end()) {
    return it - watchers.begin();
  }
  return -1;
}

DDRes context_setup_sdt_probes(const DDProfCLI &ddprof_cli,
                               DDProfContext &ctx) {
  // Check if SDT mode is off
  if (ddprof_cli.sdt_mode == "off") {
    LG_DBG("SDT probe mode is off, skipping SDT setup");
    return {};
  }

  // Check if we have an SDT allocation watcher
  int sdt_watcher_idx = context_sdt_allocation_profiling_watcher_idx(ctx);
  if (sdt_watcher_idx < 0) {
    // No SDT watcher, nothing to do
    LG_DBG("No SDT allocation watcher found, skipping SDT setup");
    return {};
  }

  // Determine target binary path
  std::string target_binary = ddprof_cli.target_binary;
  if (target_binary.empty() && !ddprof_cli.command_line.empty()) {
    target_binary = ddprof_cli.command_line[0];
    LG_DBG("Using command line first element as target binary: %s",
           target_binary.c_str());
  }

  if (target_binary.empty()) {
    if (ddprof_cli.sdt_mode == "only") {
      DDRES_RETURN_ERROR_LOG(
          DD_WHAT_INPUT_PROCESS,
          "SDT probe mode is 'only' but no target binary specified");
    }
    LG_DBG("No target binary specified, SDT probes will not be used");
    return {};
  }

  // Try to discover SDT probes
  LG_NTC("Attempting to discover SDT probes in %s", target_binary.c_str());
  auto probes = parse_sdt_probes(target_binary.c_str());

  if (!probes) {
    if (ddprof_cli.sdt_mode == "only") {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_INPUT_PROCESS,
                             "SDT probe mode is 'only' but no SDT probes found "
                             "in %s",
                             target_binary.c_str());
    }
    LG_NTC("No SDT probes found in %s, will use hook-based allocation tracking",
           target_binary.c_str());
    return {};
  }

  // Check if we have the required allocation probes
  if (!probes->has_allocation_probes()) {
    if (ddprof_cli.sdt_mode == "only") {
      DDRES_RETURN_ERROR_LOG(
          DD_WHAT_INPUT_PROCESS,
          "SDT probe mode is 'only' but required allocation probes not found "
          "in %s (need %.*s:entry/exit and %.*s:entry)",
          target_binary.c_str(), static_cast<int>(kMallocProvider.size()),
          kMallocProvider.data(), static_cast<int>(kFreeProvider.size()),
          kFreeProvider.data());
    }
    LG_NTC("Required allocation SDT probes not found in %s, will use "
           "hook-based tracking",
           target_binary.c_str());
    return {};
  }

  LG_NTC("Found %zu SDT probes in %s", probes->probes.size(),
         target_binary.c_str());

  // Get the watcher to get stack sample size
  const PerfWatcher &watcher = ctx.watchers[sdt_watcher_idx];

  // Note: We can't attach uprobes yet because we don't have the PID.
  // The uprobes will be attached later when we know the target PID.
  // For now, we just store the probe info and mark that SDT is available.

  // Store the probe set in context for later attachment
  // We'll need to attach when we know the PID (in ddprof_setup or similar)
  ctx.sdt_probes_active = true;

  LG_NTC("SDT probes discovered successfully, will attach uprobes at runtime");

  return {};
}

} // namespace ddprof
