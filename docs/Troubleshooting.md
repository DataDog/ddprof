
# Troubleshooting

## ddprof errors

### Enabling debug logs

You can increase the log level of the profiler with the `-l` option

```bash
./ddprof -l debug my_program
```

### Failures to instrument

```bash
Could not finalize watcher (idx#1): registration (Operation not permitted)
```

In order to instrument the system or target application, ddprof must call `mmap()` on a special file descriptor returned by `perf_event_open()`.  Across versions of the Linux kernel, there are slightly different ways of accounting for pinned memory limits (depending on the current EUID, system configuration, phase of the moon, etc).  Here are some ideas for mitigating that limit:

- adding `IPC_LOCK` capabilities
- `perf_event_paranoid` setting to -1
- increasing the pinned memory limits
- running fewer `ddprof` instances in parallel

In container environments, you can troubleshoot these issues by adding capabilities to your container. `CAP_SYS_ADMIN` (or `CAP_PERFMON` on newer kernels).

```bash
docker run  --cap-add CAP_PERFMON my_docker_img
```

## Reaching the agent host

It is useful to verify that the target machine can connect to a Datadog agent.  Follow the Datadog troubleshooting guidelines.

## Dev tools

### Understanding the section layout

To get an overview of the section layout, use readelf. This will help you match what is loaded in proc maps.

```
readelf -lW <binary>
```

### Reading symbols

```
nm ./path_to_binary
```

### Disassembling code

Matching the instruction pointers to assembly code. 

```
gdb --batch  -ex 'disas function_name' ./path_to_binary
```

### Dumping the dwarf information

Getting the offsets from dwarf allows us to understand the unwinding patterns
Add a filter on the instruction pointer that is relevant to the investigation

```
readelf -wF ./path_to_binary
```

example:
```
0002eb58 000000000000001c 0002eb2c FDE cie=00000030 pc=000000000015cbb5..000000000015cd1b
   LOC           CFA      rbp   ra      
000000000015cbb5 rsp+8    u     c-8   
000000000015cbb6 rsp+16   c-16  c-8   
000000000015cbb9 rbp+16   c-16  c-8   
000000000015cd1a rsp+8    c-16  c-8   
```

### Checking allocator stats

```
MALLOC_CONF=stats_print:true ./ddprof -S test-service service_cmd
```

### Library issues (LD_PRELOAD or allocation profiling)

You will want to instrument the loader function (loader.c) to figure out what is going on.
You can do this by breaking in the loader and running gdb as follows. Example of GDB using LD_PRELOAD:

```
gdb <test_prog>
set environment LD_PRELOAD=./libdd_profiling.so
b loader
run
```

Example of issue: 
A symbol is missing from the libc (compared to the musl libc where the library was compiled) 

## Speeding up builds on macOS

### Bypassing the use of shared docker volumes on macOS

Docker can be used if you are not already on a linux environment. You need an ssh configuration as some repositories are private.
The following script create a docker container based on CI dockerfiles. It will:

- Use your ssh configuration
- Automatically pull down all dependencies (same as in CI)
- Sync your files with the docker environment

```bash
./tools/launch_local_build.sh
```

To speed up builds, we recommend usage of docker-sync (shared filesystems are very slow).

1 - create a docker-sync.yml file in the root of the repo.

```yml
version: "2"
syncs:
  ddprof-sync:
    sync_strategy: "native_osx"
    src: "./"
    host_disk_mount_mode: "cached"
```

2 - Then create a docker volume and launch docker-sync

```bash
docker volume create ddprof-sync
docker-sync start # launchs a watcher that syncs the files (takes a long time on first run)
```

3 - Use the docker build environment as usual (it will pick up the docker volume from the docker-sync file)

```bash
./tools/launch_local_build.sh
```

4 - You can stop and clean these volumes after usage

```bash
docker-sync stop
docker-sync clean
docker volume rm ddprof-sync
```