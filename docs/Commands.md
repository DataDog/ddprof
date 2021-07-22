# ddprof Commands

```bash
 usage: ddprof [--help] [PROFILER_OPTIONS] COMMAND [COMMAND_ARGS]
 eg: ddprof -A hunter2 -H localhost -P 8192 redis-server /etc/redis/redis.conf

Options:
  -A, --apikey, (envvar: DD_API_KEY)
    A valid Datadog API key.  Passing the API key will cause ddprof to bypass
    the Datadog agent.  Erroneously adding this key might break an otherwise
    functioning deployment!

  -E, --environment, (envvar: DD_ENV)
    The name of the environment to use in the Datadog UI.

  -H, --host, (envvar: DD_AGENT_HOST)
    The hostname to use for intake.  This is either the hostname for the agent
    or the backend endpoint, if bypassing the agent.

  -P, --port, (envvar: DD_TRACE_AGENT_PORT)
    The intake port for the Datadog agent or backend system.

  -S, --service, (envvar: DD_SERVICE)
    The name of this service

  -d, --enable, (envvar: DD_PROFILING_ENABLED)
    Whether to enable DataDog profiling.  If this is true, then ddprof as well
    as any other DataDog profilers are enabled.  If false, they are all disabled.
    Note: if this is set, the native profiler will set the DD_PROFILING_ENABLED
    environment variable in all sub-environments, thereby enabling DataDog profilers.
    default: on

  -n, --native_enable, (envvar: DD_PROFILING_NATIVE_ENABLED)
    Whether to enable ddprof specifically, without altering how other DataDog
    profilers are run.  For example, DD_PROFILING_ENABLED can be used to disable
    an inner profile, whilst setting DD_PROFILING_NATIVE_ENABLED to enable ddprof

  -u, --upload_period, (envvar: DD_PROFILING_UPLOAD_PERIOD)
    In seconds, how frequently to upload gathered data to Datadog.
    Currently, it is recommended to keep this value to 60 seconds, which is
    also the default.

  -s, --faultinfo, (envvar: DD_PROFILING_NATIVEFAULTINFO)
    If ddprof encounters a critical error, print a backtrace of internal
    functions for diagnostic purposes.  Values are `on` or `off`
    (default: off)

  -a, --printargs, (envvar: DD_PROFILING_NATIVEPRINTARGS)
    Whether or not to print configuration parameters to the trace log.  Can
    be `yes` or `no` (default: `no`).

  -f, --sendfinal, (envvar: DD_PROFILING_NATIVESENDFINAL)
    Determines whether to emit the last partial export if the instrumented
    process ends.  This is almost never useful.  Default is `no`.

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

  -e, --event:
    A string representing the events to sample.  Defaults to `cw`
    See the `events` section below for more details.
    eg: --event sCPU --event hREF

  -v, --version:
    Prints the version of ddprof and exits.

Events
ddprof can register to various system events in order to customize the
information retrieved during profiling.  Note that certain events can add
more overhead during profiling; be sure to test your service under a realistic
load simulation to ensure the desired forms of profiling are acceptable.

The listing below gives the string to pass to the --event argument, a
brief description of the event, the name of the event as it will appear in
the Datadog UI, and the units.
Events with the same name in the UI conflict with each other; be sure to pick
only one such event!

hCPU       - CPU Cycles      (cpu-cycle, cycles)
hREF       - Ref. CPU Cycles (ref-cycle, cycles)
hINSTR     - Instr. Count    (cpu-instr, instructions)
hCREF      - Cache Ref.      (cache-ref, events)
hCMISS     - Cache Miss      (cache-miss, events)
hBRANCH    - Branche Instr.  (branch-instr, events)
hBMISS     - Branch Miss     (branch-miss, events)
hBUS       - Bus Cycles      (bus-cycle, cycles)
hBSTF      - Bus Stalls(F)   (bus-stf, cycles)
hBSTB      - Bus Stalls(B)   (bus-stb, cycles)
sCPU       - CPU Time        (cpu-time, nanoseconds)
sWALL      - Wall? Time      (wall-time, nanoseconds)
sCI        - Ctext Switches  (switches, events)
kBLKI      - Block-Insert    (block-insert, events)
kBLKS      - Block-Issue     (block-issue, events)
kBLKC      - Block-Complete  (block-complete, events)
bMalloc    - Malloc          (malloc, events)
```
