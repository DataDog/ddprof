#include "ddprof_input.h"

#include <assert.h>
#include <getopt.h>
#include <stdio.h>

#include "ddprof_cmdline.h"
#include "perf_option.h"
#include "version.h"

/************************ Options Table Helper Macros *************************/
#define X_FREE(a, b, c, d, e, f, g, h, i) FREE_EXP(i b, f);
#define X_LOPT(a, b, c, d, e, f, g, h, i) {#b, e, 0, d},
#define X_DFLT(a, b, c, d, e, f, g, h, i) DFLT_EXP(#a, i b, f, g, h);
#define X_OSTR(a, b, c, d, e, f, g, h, i) #c ":"
#define X_CASE(a, b, c, d, e, f, g, h, i) CASE_EXP(d, f, i b)

// TODO das210603 I don't think this needs to be inlined as a macro anymore
#define DFLT_EXP(evar, key, targ, func, dfault)                                \
  __extension__({                                                              \
    char *_buf = NULL;                                                         \
    if (!((targ)->key)) {                                                      \
      if (getenv(evar))                                                        \
        _buf = strdup(getenv(evar));                                           \
      else if (*dfault)                                                        \
        _buf = strdup(dfault);                                                 \
      (targ)->key = _buf;                                                      \
    }                                                                          \
  })

#define CASE_EXP(casechar, targ, key)                                          \
  case casechar:                                                               \
    if ((targ)->key)                                                           \
      free((void *)(targ)->key);                                               \
    (targ)->key = strdup(optarg);                                              \
    break;

// TODO das210603 I don't think this needs to be inlined as a macro anymore
#define FREE_EXP(key, targ)                                                    \
  __extension__({                                                              \
    free((void *)(targ)->key);                                                 \
    (targ)->key = NULL;                                                        \
  })

#define X_HLPK(a, b, c, d, e, f, g, h, i)                                      \
  "  -" #c ", --" #b ", (envvar: " #a ")",

// Helpers for exapnding the OPT_TABLE here
#define X_PRNT(a, b, c, d, e, f, g, h, i)                                      \
  if ((f)->i b)                                                                \
    LG_PRINT("  " #b ": %s", (f)->i b);

// We use a non-NULL definition for an undefined string, because it's important
// that this table is always populated intentionally.  This is checked in the
// loop inside ddprof_print_help()
// clang-format off
#define STR_UNDF (char*)1
char* help_str[DD_KLEN] = {
  [DD_API_KEY] =
"    A valid Datadog API key.  Passing the API key will cause "MYNAME" to bypass\n"
"    the Datadog agent.  Do NOT specify if you are running with an agent!\n",
  [DD_ENV] =
"    The name of the environment to use in the Datadog UI.\n",
  [DD_AGENT_HOST] =
"    The hostname of the agent. Port should also be specified.\n",
  [DD_SITE] =
"    Site url when bypassing the agent (directly pointing to intake backend).",
"    Expected url should have the form datadoghq.com\n",
"    Refer to the url / network traffic section in the docs\n",
  [DD_TRACE_AGENT_PORT] =
"    The intake port for the Datadog agent or backend system.\n",
  [DD_SERVICE] =
"    The name of this service\n",
  [DD_TAGS] = 
"    Tags sent with both profiler metrics and profiles.\n"
"    Refer to the Datadog tag section to understand what is supported",
  [DD_VERSION] =
"    Version of the service being profiled. Added to the tags during export.\n",
  [DD_PROFILING_ENABLED] =
"    Whether to enable Datadog profiling.  If this is true, then "MYNAME" as well\n"
"    as any other Datadog profilers are enabled.  If false, they are all disabled.\n"
"    Note: if this is set, the native profiler will set the DD_PROFILING_ENABLED\n"
"    environment variable in all sub-environments, thereby enabling Datadog profilers.\n"
"    default: on\n",
  [DD_PROFILING_NATIVE_ENABLED] =
"    Whether to enable "MYNAME" specifically, without altering how other Datadog\n"
"    profilers are run.  For example, DD_PROFILING_ENABLED can be used to disable\n"
"    an inner profile, whilst setting DD_PROFILING_NATIVE_ENABLED to enable "MYNAME"\n",
  [DD_PROFILING_UPLOAD_PERIOD] =
"    In seconds, how frequently to upload gathered data to Datadog.  Defaults to 60\n"
"    This value almost never needs to be changed.\n",
  [DD_PROFILING_WORKER_PERIOD] =
"    The number of uploads after which the current worker process is retired.\n"
"    This gives the user some ability to control the tradeoff between memory and\n"
"    performance.  If default values are used for this and the upload period, then\n"
"    workers are retired every four hours.\n"
"    This value almost never needs to be changed.\n",
  [DD_PROFILING_NATIVEPRINTARGS] =
"    Whether or not to print configuration parameters to the trace log.  Can\n"
"    be `yes` or `no` (default: `no`).\n",
  [DD_PROFILING_NATIVEFAULTINFO] =
"    If "MYNAME" encounters a critical error, print a backtrace of internal\n"
"    functions for diagnostic purposes.  Values are `on` or `off`\n"
"    (default: off)\n",
  [DD_PROFILING_NATIVEDUMPS] =
"    Whether "MYNAME" is able to emit coredumps on failure.\n"
"    (default: off)\n",
  [DD_PROFILING_NATIVENICE] =
"    Sets the nice level of "MYNAME" without affecting any instrumented\n"
"    processes.  This is useful on small containers with spiky workloads.\n"
"    If this parameter isn't given, then the nice level is unchanged.\n",
  [DD_PROFILING_NATIVELOGMODE] =
"    One of `stdout`, `stderr`, `syslog`, or `disabled`.  Default is `stdout`.\n"
"    If a value is given but it does not match the above, it is treated as a\n"
"    filesystem path and a log will be appended there.  Log files are not\n"
"    cleared between runs and a service restart is needed for log rotation.\n",
  [DD_PROFILING_NATIVELOGLEVEL] =
"    One of `debug`, `notice`, `warn`, `error`.  Default is `warn`.\n",
  [DD_PROFILING_NATIVESENDFINAL] =
"    Determines whether to emit the last partial export if the instrumented\n"
"    process ends.  This is almost never useful.  Default is `no`.\n",
  [DD_PROFILING_NATIVETARGET] =
"    Instrument the given PID rather than launching a new process.\n",
  [DD_PROFILING_NATIVEGLOBAL] =
"    Instruments the whole system.  Overrides DD_PROFILING_NATIVETARGET.\n"
"    Requires specific permissions or a perf_event_paranoid value of less than 1.\n",
  [DD_PROFILING_EXPORT] =
"    Defines if profiles are exported through a HTTP call.\n"
"    This is useful for developers/investigations.\n",
  [DD_PROFILING_INTERNALSTATS] =
"    Statsd socket to send internal profiler stats.\n",
};
// clang-format on

char *help_key[DD_KLEN] = {OPT_TABLE(X_HLPK)};

// clang-format off
void ddprof_print_help() {
  char help_hdr[] = ""
" usage: "MYNAME" [--help] [PROFILER_OPTIONS] COMMAND [COMMAND_ARGS]\n"
" eg: "MYNAME" -A hunter2 -H localhost -P 8192 redis-server /etc/redis/redis.conf\n\n";

  char help_opts_extra[] =
"  -e, --event:\n"
"    A string representing the events to sample.  Defaults to `cw`\n"
"    See the `events` section below for more details.\n"
"    eg: --event sCPU,1000 --event hREF\n\n"
"  -v, --version:\n"
"    Prints the version of "MYNAME" and exits.\n\n";

  char help_events[] =
"Events\n"
MYNAME" can register to various system events in order to customize the\n"
"information retrieved during profiling.  Note that certain events can add\n"
"more overhead during profiling; be sure to test your service under a realistic\n"
"load simulation to ensure the desired forms of profiling are acceptable.\n"
"\n"
"The listing below gives the string to pass to the --event argument, a\n"
"brief description of the event, the name of the event as it will appear in\n"
"the Datadog UI, and the units.\n"
"Events with the same name in the UI conflict with each other; be sure to pick\n"
"only one such event!\n"
"\n";
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
  printf("%s", help_events);

  for (int i = 0; i < perfoptions_nb_presets(); i++) {
    printf("%-10s - %-15s (%s, %s)\n", perfoptions_lookup_idx(i),
           perfoptions_preset(i)->desc, perfoptions_preset(i)->label,
           perfoptions_preset(i)->unit);
  }
}

// fills the default parameters for the input structure
DDRes ddprof_input_default(DDProfInput *input) {
  // avoid assuming that things were set to 0 by user
  memset(input, 0, sizeof(DDProfInput));
  exporter_input_dflt(&input->exp_input);
  // Populate default values (mutates ctx)
  OPT_TABLE(X_DFLT);
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

  char opt_short[] = "+" OPT_TABLE(X_OSTR) "e:hv";
  optind = 1; // reset argument parsing (in case we are called several times)

  while (-1 != (c = getopt_long(argc, argv, opt_short, lopts, &oi))) {
    switch (c) {
      OPT_TABLE(X_CASE)
    case 'e':;
      size_t idx;
      uint64_t sampling_value = 0;

      // Iterate through the specified events and define new watchers if any
      // of them are valid.  If the user specifies a '0' value, then that's
      // the same as using the default (equivalently, the ',0' could be omitted)
      if (process_event(optarg, perfoptions_lookup(), perfoptions_nb_presets(),
                        &idx, &sampling_value)) {
        input->watchers[input->num_watchers] = idx;
        input->sampling_value[input->num_watchers] = sampling_value;
        ++input->num_watchers;
      } else {
        LG_WRN("Ignoring invalid event (%s)", optarg);
      }
      break;
    case 'h':;
      ddprof_print_help();
      *continue_exec = false;
      break;
    case 'v':;
      print_version();
      *continue_exec = false;
      break;
    default:;
      *continue_exec = false;
      res = ddres_warn(DD_WHAT_INPUT_PROCESS);
      LG_ERR("Invalid option %s", argv[optind - 1]);
      break;
    }
  }
  input->nb_parsed_params = optind;

  return res;
}

void ddprof_print_params(const DDProfInput *input) { OPT_TABLE(X_PRNT); }

void ddprof_input_free(DDProfInput *input) { OPT_TABLE(X_FREE); }
