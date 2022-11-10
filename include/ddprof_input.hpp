// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "ddres_def.hpp"
#include "exporter_input.hpp"
#include "perf_watcher.hpp"

typedef int16_t watcher_index_t;

// Refer to ddprof_help to understand these params
typedef struct DDProfInput {
  int nb_parsed_params;
  // Parameters for interpretation
  char *log_mode;
  char *log_level;
  // Input parameters
  char *show_config;
  char *show_samples;
  char *affinity;
  char *enable;
  char *native_enable;
  char *agentless;
  char *upload_period;
  char *fault_info;
  char *core_dumps;
  char *nice;
  char *pid;
  char *global;
  char *worker_period;
  char *internal_stats;
  char *tags;
  char *url;
  char *socket;
  char *metrics_socket;
  char *preset;
  char *switch_user;
  // Watcher presets
  PerfWatcher watchers[MAX_TYPE_WATCHER];
  int num_watchers;
  ExporterInput exp_input;
} DDProfInput;

/*
    This table is used for a variety of things, but primarily for dispatching
    input in a consistent way across the application.  Values may come from one
    of several places, with defaulting in the following order:
      1. Commandline argument
      2. Environment variable
      3. Application default

    And input may go to one of many places
      1. Profiling parameters
      2. User data annotations
      3. Upload parameters

  A - The enum for this value.  Also doubles as an environment variable whenever
      appropriate
  B - The field name.  It is the responsibility of other conumsers to combine
      this value with context to use the right struct
  C - Short option for input processing
  D - Short option character
  E - Long option
  F - Destination struct (WOW, THIS IS HORRIBLE)
  G - defaulting function (or NULL)
  H - Fallback value, if any
  I - Substructure, if any
*/
// clang-format off

//  A                                    B                   C   D   E   F     G     H             I
#define OPT_TABLE(XX)                                                                                                         \
  XX(DD_API_KEY,                         api_key,            A, 'A', 1, input, NULL, "",           exp_input.)                \
  XX(DD_ENV,                             environment,        E, 'E', 1, input, NULL, "",           exp_input.)                \
  XX(DD_AGENT_HOST,                      host,               H, 'H', 1, input, NULL, "localhost",  exp_input.)                \
  XX(DD_SITE,                            site,               I, 'I', 1, input, NULL, "",           exp_input.)                \
  XX(DD_TRACE_AGENT_PORT,                port,               P, 'P', 1, input, NULL, "8126",       exp_input.)                \
  XX(DD_TRACE_AGENT_URL,                 url,                U, 'U', 1, input, NULL, "", )                                    \
  XX(DD_SERVICE,                         service,            S, 'S', 1, input, NULL, "myservice",  exp_input.)                \
  XX(DD_VERSION,                         service_version,    V, 'V', 1, input, NULL, "",           exp_input.)                \
  XX(DD_PROFILING_EXPORT,                do_export,          X, 'X', 1, input, NULL, "yes",        exp_input.)                \
  XX(DD_PROFILING_PPROF_PREFIX,          debug_pprof_prefix, O, 'O', 1, input, NULL, "",           exp_input.)                \
  XX(DD_PROFILING_AGENTLESS,             agentless,          L, 'L', 1, input, NULL, "", )                                    \
  XX(DD_TAGS,                            tags,               T, 'T', 1, input, NULL, "", )                                    \
  XX(DD_PROFILING_ENABLED,               enable,             d, 'd', 1, input, NULL, "yes", )                                 \
  XX(DD_PROFILING_NATIVE_ENABLED,        native_enable,      n, 'n', 1, input, NULL, "", )                                    \
  XX(DD_PROFILING_UPLOAD_PERIOD,         upload_period,      u, 'u', 1, input, NULL, "59", )                                  \
  XX(DD_PROFILING_NATIVE_WORKER_PERIOD,  worker_period,      w, 'w', 1, input, NULL, "240", )                                 \
  XX(DD_PROFILING_NATIVE_FAULT_INFO,     fault_info,         s, 's', 1, input, NULL, "yes", )                                 \
  XX(DD_PROFILING_NATIVE_CORE_DUMPS,     core_dumps,         m, 'm', 1, input, NULL, "no", )                                  \
  XX(DD_PROFILING_NATIVE_NICE,           nice,               i, 'i', 1, input, NULL, "", )                                    \
  XX(DD_PROFILING_NATIVE_SHOW_CONFIG,    show_config,        c, 'c', 1, input, NULL, "no", )                                  \
  XX(DD_PROFILING_NATIVE_LOG_MODE,       log_mode,           o, 'o', 1, input, NULL, "stdout", )                              \
  XX(DD_PROFILING_NATIVE_LOG_LEVEL,      log_level,          l, 'l', 1, input, NULL, "error", )                               \
  XX(DD_PROFILING_NATIVE_TARGET_PID,     pid,                p, 'p', 1, input, NULL, "", )                                    \
  XX(DD_PROFILING_NATIVE_GLOBAL,         global,             g, 'g', 1, input, NULL, "", )                                    \
  XX(DD_PROFILING_INTERNAL_STATS,        internal_stats,     b, 'b', 1, input, NULL, "", )                                    \
  XX(DD_PROFILING_NATIVE_SOCKET,         socket,             z, 'z', 1, input, NULL, "", )                                    \
  XX(DD_PROFILING_NATIVE_METRICS_SOCKET, metrics_socket,     k, 'k', 1, input, NULL, "/var/run/datadog-agent/statsd.sock", )  \
  XX(DD_PROFILING_NATIVE_PRESET,         preset,             D, 'D', 1, input, NULL, "", )                                    \
  XX(DD_PROFILING_NATIVE_SHOW_SAMPLES,   show_samples,       y, 'y', 0, input, NULL, "", )                                    \
  XX(DD_PROFILING_NATIVE_CPU_AFFINITY,   affinity,           a, 'a', 1, input, NULL, "", )                                    \
  XX(DD_PROFILING_NATIVE_SWITCH_USER,    switch_user,        W, 'W', 1, input, NULL, "", )
// clang-format on

#define X_ENUM(a, b, c, d, e, f, g, h, i) a,

typedef enum DDKeys { OPT_TABLE(X_ENUM) DD_KLEN } DDKeys;
#undef X_ENUM

// Fill a ddprof input
DDRes ddprof_input_parse(int argc, char **argv, DDProfInput *input,
                         bool *continue_exec);

// fills the default parameters for the input structure
DDRes ddprof_input_default(DDProfInput *input);

// Print help
void ddprof_print_help();

// Print help
void ddprof_print_params(const DDProfInput *input);

void ddprof_input_free(DDProfInput *input);
