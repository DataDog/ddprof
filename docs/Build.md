# ddprof build

## Environment setup

ddprof is meant to build on Linux.
*Local builds on macos do not work (you don't have access to perf events).*

### Native linux

Install the required libraries described in the [app/base-env/Dockerfile](app/base-env/Dockerfile).
Checkout the [Build Commands section](#build-commands).

### Docker

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

## Build commands

### Building the native profiler

```bash
source setup_env.sh
mkdir -p build_Release
cd build_Release
RelCmake ../
make -j 4 .
```

### Building the benchmark (collatz)

Add the argument `-DBUILD_BENCHMARKS=ON` to the cmake command line.

## Run commands

*Under construction* :building_construction:

### Setup

Write your datadog keys in a `.env_perso.yml` file in the root of the repository. Refer to the `.env.yml` file as a template.
Configurations are taken from the test/configs folder.

### Run examples

A stress test examples is available (you need to build the bench/collatz folder first)

```bash
./bench/runners/runit.sh collatz 
```

`run.sh` is a wrapper that helps give relevant arguments to the profiler.
Profile your own apps using

```bash
./bench/runners/run.sh my_own_exe 
```

## Artifacts

You can always check out the available `ddprof` and `collatz` binaries with something like:

```bash
aws-vault exec build-stable-developer -- aws s3 ls s3://binaries.ddbuild.io/ddprof/release/
```

### ddprof

**where**: binaries.ddbuild.io/ddprof/release/ddprof
**when**: automatic job in CI, promotion when `branch == main`

This is the native profiler.  It is shipped as a binary.

### collatz

**where**: binaries.ddbuild.io/ddprof/release/collatz
**when**: automatic job in CI, promotion when `branch == main`

### Base Image

**where**: 486234852809.dkr.ecr.us-east-1.amazonaws.com/ci/ddprof:base
**job**: build:base
**when**: manual job in CI

This image contains the system-level requirements for building `ddprof`.

### Build Image

**where**: 486234852809.dkr.ecr.us-east-1.amazonaws.com/ci/ddprof:build
**job**: build:deps
**when**: manual job in CI

This image layers the base, adding intermediate build artifacts from dependencies.  Since `ddprof` has a one-shot build system, the artifacts represent code and build resources rather than binary objects.  Dependencies are currently only `elfutils` and `libddprof`.

This is a benchmarking tool for the native profiler.

## Versioning

`ddprof` and `collatz` are versioned in binaries.ddbuild.io with something like the following scheme.

- `ddprof/release/ddprof`: latest stable version
- `ddprof/release/ddprof_main`: version from main branch
- `ddprof/release/ddprof_candidate`: candidate version to be deployed as stable
- `ddprof/release/ddprof_X.Y.Z_CIID-SHORTHASH`: pinned build, where CIID is the pipeline ID and SHORTHASH is the short commit hash.

`ddprof` and `collatz` report their version information.

```bash
ddprof --version
```
