# ddprof

The Datadog Native Profiler for Linux

## Overview

`ddprof` is a commandline utility for engaging kernel-mediated telemetry of an application and forwarding the resulting information to the Datadog backend.  In several ways, it's similar to the `perf record` tool.

## Quick Start

Our official documentation is available [here](https://docs.datadoghq.com/profiler/enabling/ddprof/?tab=environmentvariables).

### From binary

Check out our Release page for prebuilt binaries. Download the desired binary, making sure to mark it executable `chmod +x ./ddprof`.
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

- Minimal interference to execution of instrumented processes
- `ddprof`'s Memory usage is sandboxed

## Docs

Architectural showpieces and such will always be available in the `docs/` folder.

- [Build](./docs/Build.md)
- [Design](./docs/Design.md)
- [Automatically updated list of commads](./docs/Commands.md)

## Reaching-out

Any contribution is welcome. Please check out our guidelines [here](CONTRIBUTING.md).
