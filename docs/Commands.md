# ddprof Commands

```bash
ddprof is a command line utility to gather profiling data and visualize it in the Datadog UI.
You can continuously inspect where your application is spending CPU and memory.
 eg: ddprof -S service_name -H localhost -P 8192 redis-server /etc/redis/redis.conf

Usage: ddprof [OPTIONS] [command_line...]

Positionals:
  command_line TEXT ... Excludes: --pid --global
                              Your command line (including arguments)
                              This runs profiling on the given command line.
                              Incompatible with PID or Global modes.

Options:
  -h,--help                   Print this help message and exit
  -S,--service TEXT [myservice]  (Env:DD_SERVICE)
                              The name of the profiled service. Profiles are grouped by service.
  -E,--environment TEXT (Env:DD_ENV)
                              The name of the environment to use in the Datadog UI.
  -V,--service_version,--service-version TEXT (Env:DD_VERSION)
                              Version of the service being profiled.
  -U,--url TEXT (Env:DD_TRACE_AGENT_URL)
                              A <hostname>:<port> URL.  Either <hostname>:<port>, http://<hostname>:<port>
                              or https://<hostname>:<port> 
                              or unix:///run/apm.sock
                              
  -H,--host TEXT [localhost]  (Env:DD_AGENT_HOST)
                              The hostname of the agent. This is combined with the port option to generate a URL
                              
  -P,--port TEXT [8126]  (Env:DD_TRACE_AGENT_PORT)
                              The communication port for the Datadog agent. This is combined with the host option to generate a URL
                              
  -T,--tags TEXT (Env:DD_TAGS)
                              Tags attached to the profiler's data
                              Specified as a list of tags, ie: key1:value1,key2:value2
                              


Profiling settings:
  -p,--pid INT Excludes: command_line --global
                              Instrument the given PID rather than launching a new process.
  -g,--global Excludes: command_line --pid
                              Instrument all processes.
                              Requires specific capabilities or a perf_event_paranoid value of less than 1.
  -I,--inlined_functions,--inlined-functions BOOLEAN [0]  (Env:DD_PROFILING_INLINED_FUNCTIONS)
                              Report inlined functions in call stacks.
                              This is possible if debug sections are available.
                              This can have performance impacts for the profiler.
  -t,--timeline (Env:DD_PROFILING_TIMELINE_ENABLE)
                              Enables Timeline view in the Datadog UI.
                              Works by adding timestmaps to certain events.
  -u,--upload_period,--upload-period UINT [59]  (Env:DD_PROFILING_UPLOAD_PERIOD)
                              Upload period for profiles (in seconds).
                              
  -e,--event TEXT ... (Env:DD_PROFILING_NATIVE_EVENTS)
                              Customize the events we instrument. For more help, use -e help.
  --preset TEXT (Env:DD_PROFILING_NATIVE_PRESET)
                              Select a predefined profiling configuration.Available presets:
                                - default: profile CPU and memory allocations
                                   (profile only CPU when targeting a given PID)
                                - cpu_only: profile CPU
                                - alloc_only: profile memory allocations
                                - cpu_live_heap: profile live allocations and CPU
                              


Advanced settings:
  --switch_user,--switch-user TEXT
                              Run my application with a different user.
                              
  --nice INT                  Niceness (priority of process) for the profiler.
                              Higher value means nicer (lower priority).
                              
  --config [./ddprof.toml]    A configuration file
                              Check the capture_config to generate the initial file


Debug options:
  -l,--log_level,--log-level TEXT:{debug,informational,notice,warn,error} [error]  (Env:DD_PROFILING_NATIVE_LOG_LEVEL)
                              One of debug, informational, notice, warn, error.
  -o,--log_mode,--log-mode TEXT [stdout]  (Env:DD_PROFILING_NATIVE_LOG_MODE)
                              One of stdout, stderr, syslog, or disabled.
  --show_config,--show-config [0] 
                              Display the configuration.
  -b,--internal_stats,--internal-stats TEXT (Env:DD_PROFILING_INTERNAL_STATS)
                              Enables statsd metrics for ddprof. Value should point to a statsd socket.
                              Example: /var/run/datadog-agent/statsd.sock
  --show_samples,--show-samples
                              Display captured samples as logs.
                              
  -v,--version                Display the profiler's version.
                              
  --enable BOOLEAN [1]  (Env:DD_PROFILING_ENABLED)
                              Option to disable the profiler.
                              The profiler then acts as a passthrough.
                              
  --capture_config,--capture-config TEXT
                              Capture the current configuration to a file.
                              You can then give this configuration through --config.
                              

```
