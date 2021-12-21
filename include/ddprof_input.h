// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.h"
#include "ddres_def.h"
#include "exporter_input.h"

typedef int16_t watcher_index_t;

// Refer to ddprof_help to understand these params
typedef struct DDProfInput {
  int nb_parsed_params;
  // Parameters for interpretation
  char *logmode;
  char *loglevel;
  // Input parameters
  char *printargs;
  char *count_samples;
  char *enable;
  char *native_enable;
  char *agentless;
  char *upload_period;
  char *faultinfo;
  char *coredumps;
  char *nice;
  char *sendfinal;
  char *pid;
  char *global;
  char *worker_period;
  char *internalstats;
  char *tags;
  // Watcher presets
  watcher_index_t watchers[MAX_TYPE_WATCHER];
  uint64_t sampling_value[MAX_TYPE_WATCHER];
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

#define EXPORTER_OPT_TABLE(XX)        \

//  A                              B                          C   D   E   F     G     H              I
#define OPT_TABLE(XX)                                                                                            \
  XX(DD_API_KEY,                   apikey,                    A, 'A', 1, input, NULL, "",           exp_input.)  \
  XX(DD_ENV,                       environment,               E, 'E', 1, input, NULL, "",           exp_input.)  \
  XX(DD_AGENT_HOST,                host,                      H, 'H', 1, input, NULL, "localhost",  exp_input.)  \
  XX(DD_SITE,                      site,                      I, 'I', 1, input, NULL, "",           exp_input.)  \
  XX(DD_TRACE_AGENT_PORT,          port,                      P, 'P', 1, input, NULL, "8126",       exp_input.)  \
  XX(DD_SERVICE,                   service,                   S, 'S', 1, input, NULL, "myservice",  exp_input.)  \
  XX(DD_VERSION,                   serviceversion,            V, 'V', 1, input, NULL, "",           exp_input.)  \
  XX(DD_PROFILING_EXPORT,          do_export,                 X, 'X', 1, input, NULL, "yes",        exp_input.)  \
  XX(DD_PROFILING_AGENTLESS,       agentless,                 L, 'L', 1, input, NULL, "",                     )  \
  XX(DD_TAGS,                      tags,                      T, 'T', 1, input, NULL, "", )                      \
  XX(DD_PROFILING_ENABLED,         enable,                    d, 'd', 1, input, NULL, "yes", )                   \
  XX(DD_PROFILING_NATIVE_ENABLED,  native_enable,             n, 'n', 1, input, NULL, "yes", )                   \
  XX(DD_PROFILING_UPLOAD_PERIOD,   upload_period,             u, 'u', 1, input, NULL, "60", )                    \
  XX(DD_PROFILING_WORKER_PERIOD,   worker_period,             w, 'w', 1, input, NULL, "240", )                   \
  XX(DD_PROFILING_NATIVEFAULTINFO, faultinfo,                 s, 's', 1, input, NULL, "yes", )                   \
  XX(DD_PROFILING_NATIVEDUMPS,     coredumps,                 m, 'm', 1, input, NULL, "no", )                    \
  XX(DD_PROFILING_NATIVENICE,      nice,                      i, 'i', 1, input, NULL, "", )                      \
  XX(DD_PROFILING_NATIVEPRINTARGS, printargs,                 a, 'a', 1, input, NULL, "no", )                    \
  XX(DD_PROFILING_NATIVESENDFINAL, sendfinal,                 f, 'f', 1, input, NULL, "", )                      \
  XX(DD_PROFILING_NATIVELOGMODE,   logmode,                   o, 'o', 1, input, NULL, "stdout", )                \
  XX(DD_PROFILING_NATIVELOGLEVEL,  loglevel,                  l, 'l', 1, input, NULL, "error", )                 \
  XX(DD_PROFILING_NATIVETARGET,    pid,                       p, 'p', 1, input, NULL, "", )                      \
  XX(DD_PROFILING_NATIVEGLOBAL,    global,                    g, 'g', 1, input, NULL, "", )                      \
  XX(DD_PROFILING_INTERNALSTATS,   internalstats,             b, 'b', 1, input, NULL, "/var/run/datadog-agent/statsd.sock", )
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
