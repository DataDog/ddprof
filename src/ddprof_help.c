#include "ddprof.h"

#include <assert.h>
#include <stdio.h>

#define X_HLPK(a, b, c, d, e, f, g, h) "  -" #c ", --" #b ", (envvar: " #a ")",

// We use a non-NULL definition for an undefined string, because it's important
// that this table is always populated intentionally.  This is checked in the
// loop inside print_help()
// clang-format off
#define STR_UNDF (char*)1
char* help_str[DD_KLEN] = {
  [DD_API_KEY] =
"    A valid Datadog API key.  Passing the API key will cause "MYNAME" to bypass\n"
"    the Datadog agent.  Erroneously adding this key might break an otherwise\n"
"    functioning deployment!\n",
  [DD_ENV] =
"    The name of the environment to use in the Datadog UI.\n",
  [DD_AGENT_HOST] =
"    The hostname to use for intake.  This is either the hostname for the agent\n"
"    or the backend endpoint, if bypassing the agent.\n",
  [DD_SITE] = STR_UNDF,
  [DD_TRACE_AGENT_PORT] =
"    The intake port for the Datadog agent or backend system.\n",
  [DD_SERVICE] =
"    The name of this service\n",
  [DD_TAGS] = STR_UNDF,
  [DD_VERSION] = STR_UNDF,
  [DD_PROFILING_ENABLED] =
"    Whether to enable DataDog profiling.  If this is true, then "MYNAME" as well\n"
"    as any other DataDog profilers are enabled.  If false, they are all disabled.\n"
"    Note: if this is set, the native profiler will set the DD_PROFILING_ENABLED\n"
"    environment variable in all sub-environments, thereby enabling DataDog profilers.\n"
"    default: on\n",
  [DD_PROFILING_NATIVE_ENABLED] =
"    Whether to enable "MYNAME" specifically, without altering how other DataDog\n"
"    profilers are run.  For example, DD_PROFILING_ENABLED can be used to disable\n"
"    an inner profile, whilst setting DD_PROFILING_NATIVE_ENABLED to enable "MYNAME"\n",
  [DD_PROFILING_COUNTSAMPLES] = STR_UNDF,
  [DD_PROFILING_UPLOAD_PERIOD] =
"    In seconds, how frequently to upload gathered data to Datadog.  Defaults to 60\n"
"    This value almost never needs to be changed.\n",
  [DD_PROFILING_WORKER_PERIOD] =
"    The number of uploads after which the current worker process is retired.\n"
"    This gives the user some ability to control the tradeoff between memory and\n"
"    performance.  If default values are used for this and the upload period, then\n"
"    workers are retired every four hours.\n"
"    This value almost never needs to be changed.\n",
  [DD_PROFILING_CACHE_PERIOD] =
"    The number of uploads after which to clear unwinding caches.  The default\n"
"    value is 15.\n"
"    This value almost never needs to be changed.\n",
  [DD_PROFILE_NATIVEPROFILER] = STR_UNDF,
  [DD_PROFILING_] = STR_UNDF,
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
"    Instruments the whole system.  Overrides DD_PROFILING_NATIVETARGET.\n",
};
// clang-format on

char *help_key[DD_KLEN] = {OPT_TABLE(X_HLPK)};

// clang-format off
void print_help() {
  char help_hdr[] = ""
" usage: "MYNAME" [--help] [PROFILER_OPTIONS] COMMAND [COMMAND_ARGS]\n"
" eg: "MYNAME" -A hunter2 -H localhost -P 8192 redis-server /etc/redis/redis.conf\n\n";

  char help_opts_extra[] =
"  -e, --event:\n"
"    A string representing the events to sample.  Defaults to `cw`\n"
"    See the `events` section below for more details.\n"
"    eg: --event sCPU --event hREF\n\n"
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
