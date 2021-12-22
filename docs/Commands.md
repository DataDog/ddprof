# ddprof Commands

```bash
 usage: ddprof [--help] [PROFILER_OPTIONS] COMMAND [COMMAND_ARGS]
 eg: ddprof -A hunter2 -H localhost -P 8192 redis-server /etc/redis/redis.conf

Options:
  -E, --environment, (envvar: DD_ENV)
    The name of the environment to use in the Datadog UI.

  -H, --host, (envvar: DD_AGENT_HOST)
    The hostname of the agent. Port should also be specified.

  -P, --port, (envvar: DD_TRACE_AGENT_PORT)
    The communication port for the Datadog agent or backend system.

  -U, --url, (envvar: DD_TRACE_AGENT_URL)
    A <hostname>:<port> URL.  Either <hostname>:<port>, http://<hostname>:<port>
    or https://<hostname>:<port> are valid.  Overrides any other specification for
    the host or port, except if the URL is specified without a port, such as
    http://myhost.domain.com, in which case the port can be specified separately

  -S, --service, (envvar: DD_SERVICE)
    The name of this service.  It is useful to populate this field, as it will
    make it easier to locate and filter interesting profiles.
    For global mode, note that all application-level profiles are consolidated in
    the same view.

  -V, --serviceversion, (envvar: DD_VERSION)
    Version of the service being profiled. Added to the tags during export.
    This is an optional field, but it is useful for locating and filtering
    regressions or interesting behavior.

  -T, --tags, (envvar: DD_TAGS)
    Tags sent with both profiler metrics and profiles.
    Refer to the Datadog tag section to understand what is supported.

  -d, --enable, (envvar: DD_PROFILING_ENABLED)
    Whether to enable Datadog profiling.  If this is true, then ddprof as well
    as any other Datadog profilers are enabled.  If false, they are all disabled.
    Note: if this is set, the native profiler will set the DD_PROFILING_ENABLED
    environment variable in all sub-environments, thereby enabling Datadog profilers.
    default: on

  -n, --native_enable, (envvar: DD_PROFILING_NATIVE_ENABLED)
    Whether to enable ddprof specifically, without altering how other Datadog
    profilers are run.  For example, DD_PROFILING_ENABLED can be used to disable
    an inner profile, whilst setting DD_PROFILING_NATIVE_ENABLED to enable ddprof

  -i, --nice, (envvar: DD_PROFILING_NATIVENICE)
    Sets the nice level of ddprof without affecting any instrumented
    processes.  This is useful on small containers with spiky workloads.
    If this parameter isn't given, then the nice level is unchanged.

  -a, --printargs, (envvar: DD_PROFILING_NATIVEPRINTARGS)
    Whether or not to print configuration parameters to the trace log.  Can
    be `yes` or `no` (default: `no`).

  -o, --logmode, (envvar: DD_PROFILING_NATIVELOGMODE)
    One of `stdout`, `stderr`, `syslog`, or `disabled`.  Default is `stdout`.
    If a value is given but it does not match the above, it is treated as a
    filesystem path and a log will be appended there.  Log files are not
    cleared between runs and a service restart is needed for log rotation.

  -l, --loglevel, (envvar: DD_PROFILING_NATIVELOGLEVEL)
    One of `debug`, `notice`, `warn`, `error`.  Default is `warn`.

  -p, --pid, (envvar: DD_PROFILING_NATIVETARGET)
    Instrument the given PID rather than launching a new process.

  -g, --global, (envvar: DD_PROFILING_NATIVEGLOBAL)
    Instruments the whole system.  Overrides DD_PROFILING_NATIVETARGET.
    Requires specific permissions or a perf_event_paranoid value of less than 1.

  -v, --version:
    Prints the version of ddprof and exits.

```
