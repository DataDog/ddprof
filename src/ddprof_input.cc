// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_input.hpp"

#include <cassert>
#include <cstdio>
#include <getopt.h>
#include <sstream>
#include <string>

#include "constants.hpp"
#include "ddprof_cmdline.hpp"
#include "perf_watcher.hpp"
#include "version.hpp"

/************************ Options Table Helper Macros *************************/
#define X_FREE(a, b, c, d, e, f, g, h, i) FREE_EXP(i b, f);
#define X_LOPT(a, b, c, d, e, f, g, h, i) {#b, e, 0, d},
#define X_DFLT(a, b, c, d, e, f, g, h, i) DFLT_EXP(#a, i b, f, g, h);
#define X_OSTR(a, b, c, d, e, f, g, h, i) #c ":"
#define X_CASE(a, b, c, d, e, f, g, h, i) CASE_EXP(d, f, i b)

// TODO das210603 I don't think this needs to be inlined as a macro anymore
#define DFLT_EXP(evar, key, targ, func, dfault)                                \
  {                                                                            \
    char *_buf = NULL;                                                         \
    if (!((targ)->key)) {                                                      \
      if (getenv(evar))                                                        \
        _buf = strdup(getenv(evar));                                           \
      else if (*(dfault))                                                      \
        _buf = strdup(dfault);                                                 \
      (targ)->key = _buf;                                                      \
    }                                                                          \
  }

#define CASE_EXP(casechar, targ, key)                                          \
  case casechar:                                                               \
    if ((targ)->key)                                                           \
      free((void *)(targ)->key);                                               \
    if (optarg && *optarg)                                                     \
      (targ)->key = strdup(optarg);                                            \
    else                                                                       \
      (targ)->key = strdup("");                                                \
    break;

// TODO das210603 I don't think this needs to be inlined as a macro anymore
#define FREE_EXP(key, targ)                                                    \
  {                                                                            \
    free((void *)(targ)->key);                                                 \
    (targ)->key = NULL;                                                        \
  }

#define X_HLPK(a, b, c, d, e, f, g, h, i)                                      \
  "  -" #c ", --" #b ", (envvar: " #a ")",

// Helpers for expanding the OPT_TABLE here
#define X_PRNT(a, b, c, d, e, f, g, h, i)                                      \
  {                                                                            \
    if ((f)->i b) {                                                            \
      PRINT_NFO("  " #b ": %s", (f)->i b);                                     \
    }                                                                          \
  }

// We use a non-NULL definition for an undefined string, because it's important
// that this table is always populated intentionally.  This is checked in the
// loop inside ddprof_print_help()
// clang-format off
#define STR_UNDF (char*)1
const char* help_str[DD_KLEN] = {
  [DD_API_KEY] = STR_UNDF,
  [DD_ENV] =
"    The name of the environment to use in the Datadog UI.\n",
  [DD_AGENT_HOST] =
"    The hostname of the agent. Port should also be specified.\n",
  [DD_SITE] = STR_UNDF,
  [DD_TRACE_AGENT_PORT] =
"    The communication port for the Datadog agent or backend system.\n",
  [DD_TRACE_AGENT_URL] =
"    A <hostname>:<port> URL.  Either <hostname>:<port>, http://<hostname>:<port>\n"
"    or https://<hostname>:<port> are valid.  Overrides any other specification for\n"
"    the host or port, except if the URL is specified without a port, such as\n"
"    http://myhost.domain.com, in which case the port can be specified separately\n"
"    by the user.\n",
  [DD_SERVICE] =
"    The name of this service.  It is useful to populate this field, as it will\n"
"    make it easier to locate and filter interesting profiles.\n"
"    For global mode, note that all application-level profiles are consolidated in\n"
"    the same view.\n",
  [DD_VERSION] =
"    Version of the service being profiled. Added to the tags during export.\n"
"    This is an optional field, but it is useful for locating and filtering\n"
"    regressions or interesting behavior.\n",
  [DD_PROFILING_EXPORT] = STR_UNDF,
  [DD_PROFILING_PPROF_PREFIX] = STR_UNDF,
  [DD_PROFILING_AGENTLESS] = STR_UNDF,
  [DD_TAGS] =
"    Tags sent with both profiler metrics and profiles.\n"
"    Refer to the Datadog tag section to understand what is supported.\n",
  [DD_PROFILING_ENABLED] =
"    Whether to enable Datadog profiling.  If this is true, then " MYNAME " as well\n"
"    as any other Datadog profilers are enabled.  If false, they are all disabled.\n"
"    Note: if this is set, the native profiler will set the DD_PROFILING_ENABLED\n"
"    environment variable in all sub-environments, thereby enabling Datadog profilers.\n"
"    default: on\n",
  [DD_PROFILING_NATIVE_ENABLED] =
"    Whether to enable " MYNAME " specifically, without altering how other Datadog\n"
"    profilers are run.  For example, DD_PROFILING_ENABLED can be used to disable\n"
"    an inner profile, whilst setting DD_PROFILING_NATIVE_ENABLED to enable " MYNAME "\n",
  [DD_PROFILING_UPLOAD_PERIOD] = STR_UNDF,
  [DD_PROFILING_NATIVE_WORKER_PERIOD] = STR_UNDF,
  [DD_PROFILING_NATIVE_FAULT_INFO] = STR_UNDF,
  [DD_PROFILING_NATIVE_CORE_DUMPS] = STR_UNDF,
  [DD_PROFILING_NATIVE_NICE] =
"    Sets the nice level of " MYNAME " without affecting any instrumented\n"
"    processes.  This is useful on small containers with spiky workloads.\n"
"    If this parameter isn't given, then the nice level is unchanged.\n",
  [DD_PROFILING_NATIVE_SHOW_CONFIG] =
"    Whether or not to print configuration parameters to the trace log.  Can\n"
"    be `yes` or `no` (default: `no`).\n",
  [DD_PROFILING_NATIVE_LOG_MODE] =
"    One of `stdout`, `stderr`, `syslog`, or `disabled`.  Default is `stdout`.\n"
"    If a value is given but it does not match the above, it is treated as a\n"
"    filesystem path and a log will be appended there.  Log files are not\n"
"    cleared between runs and a service restart is needed for log rotation.\n",
  [DD_PROFILING_NATIVE_LOG_LEVEL] =
"    One of `debug`, `notice`, `warn`, `error`.  Default is `warn`.\n",
  [DD_PROFILING_NATIVE_TARGET_PID] =
"    Instrument the given PID rather than launching a new process.\n",
  [DD_PROFILING_NATIVE_GLOBAL] =
"    Instruments the whole system.  Overrides DD_PROFILING_NATIVE_TARGET_PID.\n"
"    Requires specific permissions or a perf_event_paranoid value of less than 1.\n",
  [DD_PROFILING_INTERNAL_STATS] = 
  "    Enables statsd metrics for " MYNAME ". Value should point to a statsd socket.\n"
  "    Example: /var/run/datadog-agent/statsd.sock\n",
  [DD_PROFILING_NATIVE_SOCKET] = STR_UNDF,
  [DD_PROFILING_NATIVE_PRESET] =
  "    Select a predefined profiling configuration.\n"
  "    Available presets:\n"
  "     - default: profile CPU and memory allocations\n"
  "       (profile only CPU when targeting a given PID)\n"
  "     - cpu_only: profile CPU\n"
  "     - alloc_only: profile memory allocations\n"
  "    If no --preset option is given, `default` preset is used.\n"
};
// clang-format on

const char *help_key[DD_KLEN] = {OPT_TABLE(X_HLPK)};

static void ddprof_input_default_events(DDProfInput *input) {
  const char *events = getenv(k_events_env_variable);
  if (!events) {
    return;
  }

  std::istringstream iss(events);
  for (std::string event_str; std::getline(iss, event_str, ';');) {
    if (event_str.empty()) {
      continue;
    }

    const char *event = event_str.c_str();
    PerfWatcher *watcher = &input->watchers[input->num_watchers];
    if (!watcher_from_event(event, watcher) &&
        !watcher_from_tracepoint(event, watcher)) {
      LG_WRN("Ignoring invalid event/tracepoint (%s)", optarg);
    } else {
      ++input->num_watchers;
    }
  }
}

// clang-format off
void ddprof_print_help() {
  char help_hdr[] = ""
" usage: " MYNAME " [--help] [PROFILER_OPTIONS] COMMAND [COMMAND_ARGS]\n"
" eg: " MYNAME " -S service_name -H localhost -P 8192 redis-server /etc/redis/redis.conf\n\n";

  char help_opts_extra[] =
"  -v, --version:\n"
"    Prints the version of " MYNAME " and exits.\n\n";
  // clang-format on

  printf("%s", help_hdr);
  printf("Options:\n");
  for (int i = 0; i < DD_KLEN; i++) {
    assert(help_str[i]);
    if (help_str[i] && STR_UNDF != help_str[i]) {
      printf("%s\n", help_key[i]);
      printf("%s\n", help_str[i]);
    }
  }
  printf("%s", help_opts_extra);
}

// fills the default parameters for the input structure
DDRes ddprof_input_default(DDProfInput *input) {
  // avoid assuming that things were set to 0 by user
  memset(input, 0, sizeof(DDProfInput));
  exporter_input_dflt(&input->exp_input);
  // Populate default values (mutates ctx)
  OPT_TABLE(X_DFLT);

  ddprof_input_default_events(input);
  return ddres_init();
}

DDRes ddprof_input_parse(int argc, char **argv, DDProfInput *input,
                         bool *continue_exec) {
  DDRes res = ddres_init();
  LG_DBG("Start parsing args");
  int c = 0, oi = 0;
  *continue_exec = true;
  struct option lopts[] = {OPT_TABLE(X_LOPT){"event", 1, 0, 'e'},
                           {"help", 0, 0, 'h'},
                           {"version", 0, 0, 'v'},
                           // Last element should be filled with 0s
                           {NULL, 0, NULL, 0}};

  //---- Process Options
  ddprof_input_default(input);

  // Early exit if the user just ran the bare command
  if (argc <= 1) {
    ddprof_print_help();
    *continue_exec = false;
    return ddres_init();
  }

  char opt_short[] = "+" OPT_TABLE(X_OSTR) "e:t:hv";
  optind = 1; // reset argument parsing (in case we are called several times)

  while (-1 != (c = getopt_long(argc, argv, opt_short, lopts, &oi))) {
    switch (c) {
      OPT_TABLE(X_CASE)
    case 'e': {
      if (!optarg || !*optarg)
        continue;

      if (input->num_watchers == MAX_TYPE_WATCHER) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_INPUT_PROCESS, "Too many input events");
      }
      PerfWatcher *watcher = &input->watchers[input->num_watchers];
      if (!watcher_from_event(optarg, watcher) &&
          !watcher_from_tracepoint(optarg, watcher)) {
        *continue_exec = false;
        DDRES_RETURN_ERROR_LOG(DD_WHAT_INPUT_PROCESS,
                               "Invalid event/tracepoint (%s)", optarg);
      } else {
        ++input->num_watchers;
      }
      break;
    }
    case 'h': {
      ddprof_print_help();
      *continue_exec = false;
      break;
    }
    case 'v': {
      print_version();
      *continue_exec = false;
      break;
    }
    default: {
      *continue_exec = false;
      DDRES_RETURN_ERROR_LOG(DD_WHAT_INPUT_PROCESS, "Invalid option %s",
                             argv[optind - 1]);
      break;
    }
    }
  }
  input->nb_parsed_params = optind;

  return res;
}

void ddprof_print_params(const DDProfInput *input) { OPT_TABLE(X_PRNT); }

void ddprof_input_free(DDProfInput *input) { OPT_TABLE(X_FREE); }
