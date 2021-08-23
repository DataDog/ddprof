[![codecov](https://codecov.io/gh/DataDog/ddprof/branch/main/graph/badge.svg?token=K8N03GBATD)](https://codecov.io/gh/DataDog/ddprof)

# ddprof

Native profiler from Datadog

## Overview

`ddprof` is a commandline utility for engaging kernel-mediated telemetry of an application and forwarding the resulting information to the Datadog backend.  In several ways, it's similar to the `perf record` tool.  Currently, `ddprof` is limited to CPU profiling of ELF binaries equipped with debuginfo.

## Quick Start

Check that your linux version is above 4.15 ([not on linux ?](#prerequisites))

```bash
uname -r
```

Check that you can access `perf_event_open()` ([more info](./docs/PerfEventParanoid.md)):
ie `perf_event_paranoid` setting is set to `<= 2` or you have `CAP_SYS_ADMIN` capabilities (required on Ubuntu / Debian).

### From binary

Download the latest ddprof binary from [here](http://binaries.ddbuild.io/ddprof/release/ddprof).  Make the binary executable : `chmod +x ./ddprof`
Refer to [commands](docs/Commands.md) for the commands supported by `ddprof`. Example :

```bash
./ddprof -S my_native_service ./run.sh
```

*Binary built with gcc-10 & ubuntu (binary compatibility :warning:).*

### From source

If you are running on a linux distribution, run :

```bash
mkdir -p build_Release
cd build_Release
cmake -DCMAKE_BUILD_TYPE=Release ../
make -j 4 .
```

More information [here](./docs/Build.md).

## Key Features

### Simplicity

`ddprof` is a wrapper, so using it should be as simple as injecting the binary into your container and wrapping your `run.sh` (or whatever) in it.  `ddprof` will use environment variables if they are available, overriding them with commandline parameters if given.  The `event` interface is fully functional, but at the time of writing the profiling backend is not configured to accept these new profile types.  Let me know if there's anything particularly interesting you'd like to instrument.

### Overhead

Full analysis of *ddprof* overhead is pending; but users of CPU-time profiling can rely on the following observations.  When there is sufficient computational headroom on the instance for *ddprof* to remain uncompetetive with the target workload (in other words, if the kernel CPU scheduler doesn't need to time-slice the target application against *ddprof*), expect *ddprof* to add less than 1% latency--usually less than 0.1%.  The precise definition of "enough" is workload dependent, but effort will be made to provide rule-of-thumb estimates once analysis has completed.

### Safety

Unlike runtime profilers, the native profiler requires no code modifications of the target service.  It doesn't direct signals at the target, use any `LD_PRELOAD` tricks, replace shared objects, or otherwise interfere with program execution at the process level once the target application has been launched.

In particular:

- While segfaults and deadlocks can interrupt profiling, they do not propagate to the target application.  A future commit will offer auto-restart options for such cases.
- PID wrapper returns the PID of the target, rather than the PID of `ddprof`.  This is great when you are already running your target under a wrapper or if you're trying to wrap the init process of a PID namespace (as might be the case for containers).

## Docs

Architectural showpieces and such will always be available in the `docs/` folder.

- [Overview on how to build and store artifacts](./docs/Build.md)
- [Discussion on perf event paranoid settings](./docs/PerfEventParanoid.md)
- [Architecture and design discussions](./docs/Design.md)
- [Automatically updated list of commads](./docs/Commands.md)

## Prerequisites

In order to take advantage of *ddprof*, you need a few things

- Linux kernel 4.15 or later (if you need to support an earlier kernel, create an issue outlining your need!  If you're blocked in a *libc* issue, also create an issue and we'll resolve it even sooner)
- Your desired application or libraries must have debuginfo.  This means they either have a `.eh_frame` or `.debug_info`.  *ddprof* will, but does not currently, support split debuginfo.
- Access to `perf events` ([more info](./docs/PerfEventParanoid.md))

## Contributing

### Features

Here are a few ideas of some interesting features. You can refer to the DataDog Jira board for a full listing.

- Profiling backend does not currently colorize flamegraphs according to code source
- `ddprof` does not support framepointers
- `ddprof` does not support split debuginfo
- `ddprof` does not furnish overhead numbers to end users
- `ddprof` does not yet implement retry if intake is unreachable

### Benchmarks

Any benchmark or feedback on usage is welcome. Specific benchmarks could address the following questions :

- For a machine with many active processes, is there more overhead when we are instrumenting a minority or when we instrument a majority (from a single invocation)?
- When the machine is non-saturated, is the distribution of latency uniform over time (or perhaps some other measurable)?

## Disclaimer

This is a *pre-beta* release.  It should not be destructive, but it may be useless. Only deploy in production if you have a very high risk tolerance, a great reversion strategy, and you've taped your pager to your face.  If you do it, please don't @ me in your postmortem :)
