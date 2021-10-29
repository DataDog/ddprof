
# Troubleshooting

## ddprof errors

### Failures to instrument

```bash
Could not finalize watcher (idx#1): registration (Operation not permitted)
```

Failure when calling mmap on perf event buffers. We are hitting the system's limitation in pinned memory usage. This can be bypassed by several actions :

- adding `IPC_LOCK` capabilities
- `perf_event_paranoid` setting to -1
- increasing the pinned memory limits
- running less `ddprof` instances in parallel

## Using the test image

A docker instance with analysis tools is available. The following sections will assume you are within the test image.

```bash
./tools/launch_local_build.sh -t
# within ddprof folder (in the app folder). Installs the tools into your path
source setup_env.sh
```

## Reaching intake service

Can you reach the intake service ? Check if you get a 400 error code. Check it also from the docker container.

```bash
curl -XPOST -i https://intake.profile.datad0g.com/v1/input
```

## Memory profiling of ddprof

You want to profile memory. Use jemalloc, then generate a svg

```bash
run.sh -j MyAppToProfile
jeprof --svg ../build_Debug/ddprof jeprof.out.220.* > /tmp/mem.svg
```

Also compiling with `JEMALLOC=ON` flag will enable periodic traces of memory state (warning large traces).

Or use massif (use the test docker image)

```bash
run.sh --massif MyAppToProfile
ms_print massif.out.*
```

## CPU profiling

ddprof and perf are the best tool for non skewed results (vs callgrind).
Here are examples using the `BadBoggleSolver_run` installed on the test docker image.

### Runnning ddprof under perf

```bash
run.sh --perfstat BadBoggleSolver_run 5
```

### callgrind

```bash
run.sh --callgrind BadBoggleSolver_run 5
```

You can then analyse results with qcachgrind (MAC) or kcachegrind (Linux).

### Benchmark tool

This tool allows you a quick overview of the performance used by the native profiler. It also measures how much the profiled application is slowed.

```bash
native_bench.sh -r 
Retrieve CPU value
CPU DIFF : 0.999 vs 1.075 
COMPUTATION DIFF: 17778 vs 16452
```

## Code coverage

The CI uses gcov.
