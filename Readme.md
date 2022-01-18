# ddprof

The Datadog Native Profiler for Linux

## Overview

`ddprof` is a commandline utility for engaging kernel-mediated telemetry of an application and forwarding the resulting information to the Datadog backend.  In several ways, it's similar to the `perf record` tool.

## Quick Start

### From binary

Check out our Release page for prebuilt binaries.  Download the desired binary, making sure to mark it executable `chmod +x ./ddprof`.
Refer to [commands](docs/Commands.md) for the commands supported by `ddprof`. Example :

```bash
./ddprof -S my_native_service ./run.sh
```

### From source

Checkout our build section [here](./docs/Build.md).

### Prerequisites

The Datadog Native Profiler for Linux has only been tested on kernel 4.15 above.  It may be supported by older kernels, but your mileage may vary.  One can verify the kernel version by running `uname`:

```bash
uname -r
```

In addition, the target machine must have `perf_event_paranoid` set to 2 or lower OR `CAP_SYS_ADMIN` enabled.

```bash
# needs to be less than or equal to 2
cat /proc/sys/kernel/perf_event_paranoid
```

Don't hesitate to [reach-out](#Reaching-out) if you are not able to use our profiler!

## Key Features

### Simplicity

`ddprof` is a wrapper, so using it should be as simple as injecting the binary into your container and wrapping your `run.sh` (or whatever) in it.  `ddprof` will use environment variables if they are available, overriding them with commandline parameters if given.

### Safety

Unlike runtime profilers, the native profiler requires no code modifications of the target service.  It doesn't direct signals at the target, use any `LD_PRELOAD` tricks, replace shared objects, or otherwise interfere with program execution at the process level once the target application has been launched.

In particular:

- While segfaults and deadlocks can interrupt profiling, they do not propagate to the target application.
- PID wrapper returns the PID of the target, rather than the PID of `ddprof`.  This is great when you are already running your target under a wrapper or if you're trying to wrap the init process of a PID namespace (as might be the case for containers).
- ddprof isolates its memory footprint to restartable sub-processes
- ddprof schedules its work on a single CPU core

## Docs

Architectural showpieces and such will always be available in the `docs/` folder.

- [Overview on how to build and store artifacts](./docs/Build.md)
- [Architecture and design discussions](./docs/Design.md)
- [Automatically updated list of commads](./docs/Commands.md)

## Contributing

Is there something you'd love to see in the tool?  Have a great idea for a new feature?  Did you find a bug?  Please feel free to submit PRs or issues as appropriate--the ddprof maintenance team will respond accordingly.
