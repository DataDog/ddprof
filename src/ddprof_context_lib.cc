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
#include "span.hpp"

#include <algorithm>
#include <charconv>
#include <errno.h>
#include <string_view>
#include <sys/sysinfo.h>
#include <unistd.h>

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

struct Preset {
  static constexpr size_t k_max_events = 10;
  const char *name;
  const char *events[k_max_events];
};

DDRes add_preset(DDProfContext *ctx, const char *preset,
                 bool pid_or_global_mode) {
  using namespace std::literals;
  static Preset presets[] = {
      {"default", {"sCPU", "sALLOC"}},
      {"default-pid", {"sCPU"}},
      {"cpu_only", {"sCPU"}},
      {"alloc_only", {"sALLOC"}},
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

void log_watcher(const PerfWatcher *w, int n) {
  PRINT_NFO("    ID: %s, Pos: %d, Index: %lu", w->desc.c_str(), n, w->config);
  switch (w->loc_type) {
  case kPerfWatcherLoc_period:
    PRINT_NFO("    Location: Sample");
    break;
  case kPerfWatcherLoc_reg:
    PRINT_NFO("    Location: Register, regno: %d", w->regno);
    break;
  case kPerfWatcherLoc_raw:
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

  if (w->options.is_freq)
    PRINT_NFO("    Cadence: Freq, Freq: %lu", w->sample_frequency);
  else
    PRINT_NFO("    Cadence: Period, Period: %lu", w->sample_period);

  if (w->output_mode & kPerfWatcherMode_callgraph)
    PRINT_NFO("    Outputting to callgraph (flamegraph)");
  if (w->output_mode & kPerfWatcherMode_metric)
    PRINT_NFO("    Outputting to metric");
}

/****************************  Argument Processor  ***************************/
DDRes ddprof_context_set(DDProfInput *input, DDProfContext *ctx) {
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

  DDRES_CHECK_FWD(exporter_input_copy(&input->exp_input, &ctx->exp_input));

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

  // URL-based host/port override
  if (input->url && *input->url) {
    LG_NTC("Processing URL: %s", input->url);
    char *delim = strchr(input->url, ':');
    char *host = input->url;
    char *port = NULL;
    if (delim && delim[1] == '/' && delim[2] == '/') {
      // A colon was found.
      // http://hostname:port -> (hostname, port)
      // ftp://hostname:port -> error
      // hostname:port -> (hostname, port)
      // hostname: -> (hostname, default_port)
      // hostname -> (hostname, default_port)

      // Drop the schema
      *delim = '\0';
      if (!strncasecmp(input->url, "http", 4) ||
          !strncasecmp(input->url, "https", 5)) {
        *delim = ':';
        host = delim + 3; // Navigate after schema
      }
      delim = strchr(host, ':');
    }

    if (delim) {
      // Check to see if there is another colon for the port
      // We're going to treat this as the port.  This is slightly problematic,
      // since an invalid port is going to invalidate the default and then throw
      // an error later, but for now let's just do what the user told us even if
      // it isn't what they wanted.  :)
      *delim = '\0';
      port = delim + 1;
    }

    // Modify the input structure to reflect the values from the URL.  This
    // overwrites an otherwise immutable parameter, which is slightly
    // unfortunate, but this way it harmonizes with the downstream movement of
    // host/port and the input arg pretty-printer.
    if (host) {
      free((char *)input->exp_input.host);
      free((char *)ctx->exp_input.host);
      input->exp_input.host = strdup(host); // For the pretty-printer
      ctx->exp_input.host = strdup(host);
    }
    if (port) {
      free((char *)input->exp_input.port);
      free((char *)ctx->exp_input.port);
      input->exp_input.port = strdup(port); // Merely for the pretty-printer
      ctx->exp_input.port = strdup(port);
    }

    // Revert the delimiter in case we want to print the URL later
    if (delim) {
      *delim = ':';
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
    if (WATCHER_FAILED == tmpwatcher || !tmpwatcher) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC,
                             "Could not allocate storage for watcher template");
    }
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

void ddprof_context_free(DDProfContext *ctx) {
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

int ddprof_context_allocation_profiling_watcher_idx(const DDProfContext *ctx) {
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
