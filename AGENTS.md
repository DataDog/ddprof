# ddprof — Agent Guide

## Repository overview

`ddprof` is the Datadog native profiler for Linux. It profiles CPU time and
memory allocations of arbitrary processes without requiring source changes.

Key source directories:
- `include/` — public and internal headers
- `src/` — profiler core (perf event handling, unwinding, pprof output)
- `src/pprof/` — pprof profile building and export
- `src/lib/` — embedded library (injected into target process)
- `test/` — unit and integration tests
- `bench/` — benchmarks
- `cmake/` — CMake modules and build type definitions
- `tools/` — helper scripts
- `app/base-env/` — Ubuntu Dockerfile
- `app/base-env-alpine/` — Alpine Dockerfile (used for releases)

## Development environment

All development happens inside Docker containers. The source tree is mounted at
`/app` inside the container.

### Launching a container

```bash
# Ubuntu 24 (default for day-to-day development)
./tools/launch_local_build.sh

# Ubuntu 18 (matches older glibc targets, libc 2.27)
./tools/launch_local_build.sh -u 18

# Alpine (musl — matches the release binary environment)
./tools/launch_local_build.sh -f ./app/base-env-alpine/Dockerfile

# Clang instead of GCC
./tools/launch_local_build.sh --clang

# Force rebuild the Docker image
./tools/launch_local_build.sh --clean
```

The script forwards your SSH agent and mounts the current directory (or a
docker-sync volume) at `/app`. The container is ephemeral — nothing outside
`/app` is preserved on exit.

### Running containers (typical dev setup)

Day-to-day iteration uses the Ubuntu 24 container. Execute commands inside it:

```bash
docker exec -it <container_name> bash
# or run a single command
docker exec <container_name> bash -c "cd /app/build_gcc_unknown-linux-2.39_Rel && make -j$(nproc) ddprof"
```

## Build system

### Setup

Inside the container, source `setup_env.sh` once per shell session to get the
CMake helper functions and update `PATH`:

```bash
source setup_env.sh
```

### Build directory naming

Build directories follow the pattern:

```
build_<compiler>_<os-triplet>_<build-type>
```

Examples:
- `build_gcc_unknown-linux-2.39_Rel` — GCC, Ubuntu 24 (glibc 2.39), Release
- `build_gcc_alpine-linux-1.2.5_Rel` — GCC, Alpine (musl 1.2.5), Release
- `build_gcc_unknown-linux-2.39_San` — GCC, Ubuntu 24, Sanitized (ASan+UBSan)
- `build_clang_unknown-linux-2.39_San` — Clang, Ubuntu 24, Sanitized

`MkBuildDir <suffix>` creates and enters the appropriately named directory.

### Common build workflows

```bash
source setup_env.sh

# Release build (fast, optimised, LTO)
MkBuildDir Rel
RelCMake ../
make -j$(nproc) ddprof

# Debug build (symbols, no optimisation)
MkBuildDir Deb
DebCMake ../
make -j$(nproc)

# Sanitized build (ASan + UBSan — catches memory bugs)
MkBuildDir San
SanCMake ../
make -j$(nproc)

# Thread sanitizer
MkBuildDir TSan
TSanCMake ../
make -j$(nproc)
```

### Build modes at a glance

| Mode | CMake helper | Build type | Use for |
|---|---|---|---|
| `Rel` | `RelCMake` | Release | Performance testing, pre-release checks |
| `Deb` | `DebCMake` | Debug | Day-to-day debugging, step-through |
| `San` | `SanCMake` | SanitizedDebug | Catching memory/UB errors |
| `TSan` | `TSanCMake` | ThreadSanitizedDebug | Catching data races |
| `AlpRel` | `RelCMake` | Release (Alpine) | **Release binary** — what ships to users |

### Alpine (release) builds

The release binary must be built on Alpine to produce a statically-linked
musl binary that runs everywhere. Use the Alpine container:

```bash
./tools/launch_local_build.sh -f ./app/base-env-alpine/Dockerfile
# inside the container:
source setup_env.sh
MkBuildDir AlpRel
RelCMake -DDDPROF_STATIC=ON ../
make -j$(nproc) ddprof
```

The resulting `ddprof` binary is fully static, compatible with both glibc and
musl systems.

## Running tests

```bash
cd /app/build_gcc_unknown-linux-2.39_Rel

# All tests
ctest -j4

# Specific test by name
ctest -R simple_malloc -V

# Re-run only failed tests
ctest --rerun-failed --output-on-failure
```

Integration tests (e.g. `simple_malloc`, `simple_malloc-with-event-reordering`)
require `CAP_SYS_PTRACE` and `SYS_ADMIN` — the launch script adds these
capabilities automatically.

### Showing samples at runtime

Pass `--show_samples` to `ddprof` to print each profiling sample to the log.
The log format is:

```
sample[type=<name>;pid=<pid>;tid=<tid>] <frames...> <value>
```

where `<name>` is the kebab-case sample type (e.g. `alloc-space`, `cpu-time`).

Useful env vars for debugging:
```bash
export DD_PROFILING_NATIVE_LOG_LEVEL=debug       # verbose logging
export DD_PROFILING_NATIVE_LOG_MODE=/tmp/ddprof.log  # log to file
export DD_PROFILING_NATIVE_USE_EMBEDDED_LIB=1    # use embedded .so (library mode)
export LD_LIBRARY_PATH=$PWD                      # find libdd_profiling-embedded.so
```

### Manual repro of integration tests

```bash
cd /app/build_gcc_unknown-linux-2.39_Rel
export DD_PROFILING_NATIVE_USE_EMBEDDED_LIB=1
export LD_LIBRARY_PATH=$PWD
log=$(mktemp $PWD/log.XXXXXX)
export DD_PROFILING_NATIVE_LOG_MODE=$log
taskset 0x1 ./ddprof --show_samples ./test/simple_malloc-static --loop 200 --spin 100
grep 'sample\[type' $log | head -20
```

## Code style

```bash
# Check formatting
./tools/style-check.sh

# Apply clang-format
git clang-format HEAD~1   # format changes since last commit
```

The project uses `clang-format` with a `.clang-format` file at the root.
Always run formatting before committing.

## Key dependencies

- **libdatadog** — Rust profiling library providing the pprof serialisation and
  export FFI. Vendored under `vendor_*/`. Updated via `tools/fetch_libdatadog.sh`.
- **elfutils** — DWARF unwinding. Vendored via `tools/fetch_elfutils.sh`.
- **abseil-cpp** — String utilities, used as a CMake dependency.
- **jemalloc** — Default allocator in release builds.
