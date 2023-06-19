// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_context_lib.hpp"

#include "ddprof_cmdline.hpp"
#include "ddprof_context.hpp"
#include "ddprof_cpumask.hpp"
#include "ddprof_input.hpp"
#include "logger.hpp"
#include "logger_setup.hpp"
#include "presets.hpp"
#include "span.hpp"
#include "ddprof_cli.hpp"

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
void copy_cli_values(const DDProfCLI &ddprof_cli, DDProfContext_V2 &ctx) {

  // possible std::move here ?
  ctx.exp_input = ddprof_cli.exporter_input;
  // todo avoid manual copies
  ctx.params.tags = ddprof_cli.tags;
  // Profiling settings
  ctx.params.pid = ddprof_cli.pid;
  ctx.params.global = ddprof_cli.global;
  ctx.params.upload_period = ddprof_cli.upload_period;
  // todo : naming ?
  ctx.params.worker_period = ddprof_cli.profiler_reset;
  // Advanced
  ctx.params.switch_user = ddprof_cli.switch_user;
  ctx.params.nice = ddprof_cli.nice;
  // Debug
  ctx.params.internal_stats = ddprof_cli.internal_stats;
  ctx.params.enable = ddprof_cli.enable;
  // todo check if this is reasonable ?
  if (!ddprof_cli.enable) {
    setenv("DD_PROFILING_ENABLED", "false", true);
  }
  // hidden
  if (!ddprof_cli.cpu_affinity.empty() && !parse_cpu_mask(ddprof_cli.cpu_affinity, ctx.params.cpu_affinity)) {
    LG_WRN("Unable to parse cpu_affinity setting");
  }

  ctx.params.show_samples = ddprof_cli.show_samples;
  ctx.params.fault_info = ddprof_cli.fault_info;

  ctx.params.sockfd = ddprof_cli.socket;
  if (ctx.params.sockfd != -1) {
    ctx.params.wait_on_socket = true;
  }
}

DDRes setup_watchers(const DDProfCLI &ddprof_cli, DDProfContext_V2 &ctx) {
  std::vector<PerfWatcher> watchers;
  DDRES_CHECK_FWD(ddprof_cli.add_watchers_from_events(watchers));
  if (const PerfWatcher *dup_watcher = find_duplicate_event(watchers);
      dup_watcher != nullptr) {
    DDRES_RETURN_ERROR_LOG(
        DD_WHAT_INPUT_PROCESS, "Duplicate event found in input: %s",
        event_type_name_from_idx(dup_watcher->ddprof_event_type));
  }

  std::string preset = ddprof_cli.preset;

  if (!preset.empty() && watchers.size() == 0) {
    // use `default` preset when no preset and no events were given in input
    preset = "default";
  }

  if (!preset.empty()) {
    bool pid_or_global_mode = (ddprof_cli.global || ddprof_cli.pid)
        && ctx.params.sockfd == -1;
    DDRES_CHECK_FWD(add_preset_v2(preset, pid_or_global_mode, watchers));
  }
  ctx.watchers = std::move(watchers);
  return {};
}
}

DDRes context_set_v2(const DDProfCLI &ddprof_cli, DDProfContext_V2 &ctx) {
  setup_logger(ddprof_cli.log_mode.c_str(), ddprof_cli.log_level.c_str());

  copy_cli_values(ddprof_cli, ctx);

  DDRES_CHECK_FWD(setup_watchers(ddprof_cli, ctx));

  if (ddprof_cli.show_config) {
    ddprof_cli.print();
    // print watcher values
  }

  return {};
}

/****************************  Argument Processor  ***************************/
DDRes context_set(DDProfInput *input, DDProfContext *ctx) {
  *ctx = {};
  setup_logger(input->log_mode, input->log_level);

  if (const PerfWatcher *dup_watcher = find_duplicate_event(
          {input->watchers, static_cast<size_t>(input->num_watchers)});
      dup_watcher != nullptr) {
    DDRES_RETURN_ERROR_LOG(
        DD_WHAT_INPUT_PROCESS, "Duplicate event found in input: %s",
        event_type_name_from_idx(dup_watcher->ddprof_event_type));
  }

  // Shallow copy of the watchers from the input object into the context.
  int nwatchers;
  for (nwatchers = 0; nwatchers < input->num_watchers; ++nwatchers) {
    ctx->watchers[nwatchers] = input->watchers[nwatchers];
  }
  ctx->num_watchers = nwatchers;

  // Set defaults
  ctx->params.upload_period = 60.0;

  // Process enable.  Note that we want the effect to hit an inner profile.
  // TODO das210603 do the semantics of this match other profilers?
  ctx->params.enable = !arg_yesno(input->enable, 0); // default yes
  if (ctx->params.enable)
    setenv("DD_PROFILING_ENABLED", "true", true);
  else
    setenv("DD_PROFILING_ENABLED", "false", true);

  // Process native profiler enablement override
  if (input->native_enable) {
    ctx->params.enable = arg_yesno(input->native_enable, 1);
  }

  // Process enablement for agent mode
  ctx->exp_input.agentless = arg_yesno(input->agentless, 1); // default no

  // process upload_period
  if (input->upload_period) {
    double x = strtod(input->upload_period, NULL);
    if (x > 0.0)
      ctx->params.upload_period = x;
  }

  ctx->params.worker_period = 240;
  if (input->worker_period) {
    char *ptr_period = input->worker_period;
    int tmp_period = strtol(input->worker_period, &ptr_period, 10);
    if (ptr_period != input->worker_period && tmp_period > 0)
      ctx->params.worker_period = tmp_period;
  }

  // Process fault_info
  ctx->params.fault_info = arg_yesno(input->fault_info, 1); // default no

  // Process core_dumps
  // This probably makes no sense with fault_info enabled, but considering that
  // there are other dumpable signals, we ignore
  ctx->params.core_dumps = arg_yesno(input->core_dumps, 1); // default no

  // Process nice level
  // default value is -1 : nothing to override
  ctx->params.nice = -1;
  if (input->nice) {
    char *ptr_nice = input->nice;
    int tmp_nice = strtol(input->nice, &ptr_nice, 10);
    if (ptr_nice != input->nice)
      ctx->params.nice = tmp_nice;
  }

  ctx->params.num_cpu = ddprof::nprocessors_conf();

  // Adjust target PID
  pid_t pid_tmp = 0;
  if (input->pid && (pid_tmp = strtol(input->pid, NULL, 10)))
    ctx->params.pid = pid_tmp;

  // Adjust global mode
  ctx->params.global = arg_yesno(input->global, 1); // default no
  if (ctx->params.global) {
    if (ctx->params.pid) {
      LG_WRN("[INPUT] Ignoring PID (%d) in param due to global mode",
             ctx->params.pid);
    }
    ctx->params.pid = -1;
  }

  // Enable or disable the propagation of internal statistics
  if (input->internal_stats) {
    ctx->params.internal_stats = strdup(input->internal_stats);
    if (!ctx->params.internal_stats) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC,
                             "Unable to allocate string for internal_stats");
    }
  }

  // Specify export tags
  if (input->tags) {
    ctx->params.tags = strdup(input->tags);
    if (!ctx->params.tags) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC,
                             "Unable to allocate string for tags");
    }
  }

  ctx->params.sockfd = -1;
  ctx->params.wait_on_socket = false;
  if (input->socket && strlen(input->socket) > 0) {
    std::string_view sv{input->socket};
    int sockfd;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.end(), sockfd);
    if (ec == std::errc() && ptr == sv.end()) {
      ctx->params.sockfd = sockfd;
      ctx->params.wait_on_socket = true;
    }
  }

  ctx->params.dd_profiling_fd = -1;

  const char *preset = input->preset;
  if (!preset && ctx->num_watchers == 0) {
    // use `default` preset when no preset and no events were given in input
    preset = "default";
  }

  if (preset) {
    bool pid_or_global_mode = ctx->params.pid && ctx->params.sockfd == -1;
    DDRES_CHECK_FWD(add_preset(ctx, preset, pid_or_global_mode));
  }

  CPU_ZERO(&ctx->params.cpu_affinity);
  if (input->affinity) {
    if (!ddprof::parse_cpu_mask(input->affinity, ctx->params.cpu_affinity)) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_INPUT_PROCESS,
                             "Invalid CPU affinity mask");
    }
  }

  DDRES_CHECK_FWD(exporter_input_copy(&input->exp_input, &ctx->exp_input));

  ddprof::span watchers{ctx->watchers, static_cast<size_t>(ctx->num_watchers)};
  if (std::find_if(watchers.begin(), watchers.end(), [](const auto &watcher) {
        return watcher.type < PERF_TYPE_MAX;
      }) == watchers.end()) {

    if (ctx->num_watchers == MAX_TYPE_WATCHER) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_INPUT_PROCESS, "Too many input events");
    }

    // if there are no perf active watcher, add a dummy watcher to be notified
    // on process exit
    const PerfWatcher *tmpwatcher = ewatcher_from_str("sDUM");
    ctx->watchers[ctx->num_watchers++] = *tmpwatcher;
  }

  order_watchers({ctx->watchers, static_cast<size_t>(ctx->num_watchers)});

  // Process input printer (do this right before argv/c modification)
  if (input->show_config && arg_yesno(input->show_config, 1)) {
    PRINT_NFO("Printing parameters -->");
    ddprof_print_params(input);

    PRINT_NFO("  Native profiler enabled: %s",
              ctx->params.enable ? "true" : "false");

    // Tell the user what mode is being used
    PRINT_NFO("  Profiling mode: %s",
              -1 == ctx->params.pid ? "global"
                  : pid_tmp         ? "target"
                                    : "wrapper");

    // Show watchers
    PRINT_NFO("  Instrumented with %d watchers:", ctx->num_watchers);
    for (int i = 0; i < ctx->num_watchers; i++) {
      log_watcher(&ctx->watchers[i], i);
    }
  }

  ctx->params.show_samples = input->show_samples != nullptr;

  if (input->switch_user) {
    ctx->params.switch_user = strdup(input->switch_user);
  }

  ctx->initialized = true;
  return ddres_init();
}

void context_free(DDProfContext *ctx) {
  if (ctx->initialized) {
    exporter_input_free(&ctx->exp_input);
    free((char *)ctx->params.internal_stats);
    free((char *)ctx->params.tags);
    free((char *)ctx->params.switch_user);
    if (ctx->params.sockfd != -1) {
      close(ctx->params.sockfd);
    }
    *ctx = {};
  }

  LOG_close();
}

int context_allocation_profiling_watcher_idx(const DDProfContext *ctx) {
  ddprof::span watchers{ctx->watchers, static_cast<size_t>(ctx->num_watchers)};
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
