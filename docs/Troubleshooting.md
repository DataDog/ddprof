
# Troubleshooting

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

## Memory profiling

You want to profiler memory. Use jemalloc, then generate a svg

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
