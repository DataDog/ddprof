// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_context_lib.h"

extern "C" {
#include "ddprof_cmdline.h"
#include "ddprof_context.h"
#include "ddprof_input.h"
#include "logger.h"
#include "logger_setup.h"
}

#include "span.hpp"

#include <algorithm>
#include <charconv>
#include <errno.h>
#include <sys/sysinfo.h>
#include <unistd.h>

/****************************  Argument Processor  ***************************/
DDRes ddprof_context_set(DDProfInput *input, DDProfContext *ctx) {
  *ctx = {};
  setup_logger(input->log_mode, input->log_level);

  // Shallow copy of the watchers from the input object into the context.
  int nwatchers;
  for (nwatchers = 0; nwatchers < input->num_watchers; ++nwatchers) {
    ctx->watchers[nwatchers] = input->watchers[nwatchers];
  }
  ctx->num_watchers = nwatchers;

  // If no events were given, install the default watcher with default freq.
  if (!ctx->num_watchers) {
    ctx->num_watchers = 1;
    ctx->watchers[0] = *ewatcher_from_str("sCPU");
  }

  ddprof::span watchers{ctx->watchers, static_cast<size_t>(ctx->num_watchers)};
  if (std::find_if(watchers.begin(), watchers.end(),
                   [](const auto &watcher) {
                     return watcher.type < PERF_TYPE_MAX;
                   }) == watchers.end() &&
      ctx->num_watchers < MAX_TYPE_WATCHER) {
    // if there are no perf active watcher, add a dummy watcher to be notified
    // on process exit
    ctx->watchers[ctx->num_watchers++] = *ewatcher_from_str("sDUM");
  }

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

  ctx->params.num_cpu = get_nprocs();

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

  // Process input printer (do this right before argv/c modification)
  if (input->show_config && arg_yesno(input->show_config, 1)) {
    LG_PRINT("Printing parameters -->");
    ddprof_print_params(input);

    LG_PRINT("  Native profiler enabled: %s",
             ctx->params.enable ? "true" : "false");

    // Tell the user what mode is being used
    LG_PRINT("  Profiling mode: %s",
             -1 == ctx->params.pid ? "global"
                 : pid_tmp         ? "target"
                                   : "wrapper");

    // Show watchers
    LG_PRINT("  Instrumented with %d watchers:", ctx->num_watchers);
    for (int i = 0; i < ctx->num_watchers; i++) {
      LG_PRINT("    ID: %s, Pos: %d, Index: %lu, Label: %s",
               ctx->watchers[i].desc, i, ctx->watchers[i].config,
               sample_type_name_from_idx(ctx->watchers[i].sample_type_id));
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

  ctx->initialized = true;
  return ddres_init();
}

void ddprof_context_free(DDProfContext *ctx) {
  if (ctx->initialized) {
    exporter_input_free(&ctx->exp_input);
    free((char *)ctx->params.internal_stats);
    free((char *)ctx->params.tags);
    *ctx = {};
    if (ctx->params.sockfd != -1) {
      close(ctx->params.sockfd);
    }
  }
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
