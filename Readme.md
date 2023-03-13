# ddprof

The Datadog Native Profiler for Linux.

## Overview

`ddprof` is a command-line utility to gather profiling data. After install you will continuously see where your application is spending CPU and memory allocations.
The data will be available in the `/profiling` section of the [Datadog UI](https://app.datadoghq.com/).

## Quick Start

Our official documentation is available [here](https://docs.datadoghq.com/profiler/enabling/ddprof/?tab=environmentvariables).
Our pre-built binaries are compatible with both musl and glibc. You should not need to recompile `ddprof` from source. 

### From binary [Recommended]

An installation guide is available [here](https://docs.datadoghq.com/profiler/enabling/ddprof/?tab=environmentvariables).
Check out our Release page for our [latest](https://github.com/DataDog/ddprof/releases/tag/latest) release. Download the release and extract `ddprof`.
Instrumenting your application should be as simple as adding `ddprof` in front of your usual command line.

To install the profiler, check out our [installation-helpers](#Installation-helpers) bellow.

The following command will run `ddprof` with the default settings (CPU and allocations)

```bash
ddprof -S service_name_for_my_program ./my_program arg1 arg2
```

Profiling data shows up in the `/profiling` section of your Datadog UI. Specifying a service name will help you select your profiling data. 
Refer to [commands](docs/Commands.md) for a more advanced usage of `ddprof`.

### From source

Checkout our build section [here](./docs/Build.md).

## Prerequisites

### Perf event paranoid

The target machine must have `perf_event_paranoid` set to 2 or lower.

```bash
# needs to be less than or equal to 2
cat /proc/sys/kernel/perf_event_paranoid
```

Here is an example adding a startup configuration to your system. This requires a system restart.

```bash
sudo sh -c 'echo kernel.perf_event_paranoid=2 > /etc/sysctl.d/perf_event_paranoid_2.conf'
```

Alternatively you can use `CAP_SYS_ADMIN` or `sudo` as a one off test mechanism, more in the [Troubleshooting](./docs/Troubleshooting.md) section. 
Don't hesitate to [reach-out](#Reaching-out) if you are not able to use our profiler!

### Agent installation

It is recommended to have an agent setup on the system you are profiling.
By default the profiler will target `localhost:8126` (the default trace agent endpoint). The `DD_TRACE_AGENT_URL` environment variable can be used to override this setting.

## Installation helpers

### Ubuntu / Debian

The following commands will download and install `ddprof` on Debian or Ubuntu distributions:

```bash
export ARCH=$(dpkg --print-architecture) # ARCH should hold amd64 or arm64
# ddprof requires xz-utils to uncompress the archive
sudo apt-get update && \
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y xz-utils curl jq && \
tag_name=$(curl -s https://api.github.com/repos/DataDog/ddprof/releases/latest | jq -r '.tag_name[1:]') && \
url_release="https://github.com/DataDog/ddprof/releases/download/v${tag_name}/ddprof-${tag_name}-${ARCH}-linux.tar.xz" && \
curl -L -o ddprof-${ARCH}-linux.tar.xz ${url_release} && \
tar xvf ddprof-${ARCH}-linux.tar.xz && \
sudo mv ddprof/bin/ddprof /usr/local/bin && \
rm -Rf ddprof-amd64-linux.tar.xz ./ddprof && \
ddprof --version
```

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
