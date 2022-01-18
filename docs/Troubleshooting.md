
# Troubleshooting

## ddprof errors

### Failures to instrument

```bash
Could not finalize watcher (idx#1): registration (Operation not permitted)
```

In order to instrument the system or target application, ddprof must call `mmap()` on a special file descriptor returned by `perf_event_open()`.  Across versions of the Linux kernel, there are slightly different ways of accounting for pinned memory limits (depending on the current EUID, system configuration, phase of the moon, etc).  Here are some ideas for mitigating that limit:

- adding `IPC_LOCK` capabilities
- `perf_event_paranoid` setting to -1
- increasing the pinned memory limits
- running fewer `ddprof` instances in parallel

## Using the test image

A docker instance with analysis tools is available. The following sections will assume you are within the test image.  This can be done with:

```bash
./tools/launch_local_build.sh -t
cd build # note that the build directory is shared with the host; you may have to clear this directory or create a different one
source ../setup_env.sh # loads common invocations for cmake--not necessary, but can improve your quality-of-life
```

## Reaching the agent host

It is useful to verify that the target machine can connect to a Datadog agent.  Follow the Datadog troubleshooting guidelines.

