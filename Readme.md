# ddprof

The Datadog Native Profiler for Linux.

## Overview

`ddprof` is a command-line utility to gather profiling data. After install you will continuously see where your application is spending CPU and memory allocations.
The data will be available in the `/profiling` section of the Datadog UI.

## Quick Start

Our official documentation is available [here](https://docs.datadoghq.com/profiler/enabling/ddprof/?tab=environmentvariables).
Our pre-built binaries are compatible with both musl and glibc. You should not need to recompile `ddprof` from source. 

### From binary [Recommended]

A full installation guide is available [here](https://docs.datadoghq.com/profiler/enabling/ddprof/?tab=environmentvariables).
Check out our Release page for our latest release. Download the release and extract `ddprof`.
Instrumenting your application should be as simple as adding `./ddprof` in front of your usual command line.

```bash
./ddprof -S service_name_for_my_program ./my_program arg1 arg2
```

Profiling data shows up in the `/profiling` section of your Datadog UI. Specifying a service name will help you select your profiling data. 
Refer to [commands](docs/Commands.md) for a more advanced usage of `ddprof`.

### From source

Checkout our build section [here](./docs/Build.md).

### Prerequisites

#### Perf event paranoid

The target machine must have `perf_event_paranoid` set to 2 or lower OR `CAP_SYS_ADMIN` enabled.
Some distributions have set the default to higher values (up to 4), which will prevent `ddprof` from instrumenting CPU activity through the perf_event_open API.

```bash
# needs to be less than or equal to 2
cat /proc/sys/kernel/perf_event_paranoid
```

Don't hesitate to [reach-out](#Reaching-out) if you are not able to use our profiler!

#### Agent installation

It is recommended to have an agent setup on the system you are profiling.
By default the profiler will target `localhost:8126` (the default trace agent endpoint). The `DD_TRACE_AGENT_URL` environment variable can be used to override this setting.

## Key Features

### Simplicity

`ddprof` is a wrapper, so using it should be as simple as injecting the binary into your container and wrapping your entrypoint.
`ddprof` will use environment variables if they are available, overriding them with commandline parameters if given.

### Safety

- Minimal interference to execution of instrumented processes
- `ddprof`'s Memory usage is sandboxed

### Allocation profiling

- By working in user space, `ddprof` can instrument allocations with low overhead

## Docs

Architectural showpieces and such will always be available in the `docs/` folder.

- [Build](./docs/Build.md)
- [Design](./docs/Design.md)
- [Automatically updated list of commads](./docs/Commands.md)
- [Troubleshooting](./docs/Troubleshooting.md)

## Reaching-out

Any contribution is welcome. Please check out our guidelines [here](CONTRIBUTING.md).
