# ddprof build

## Environment setup

ddprof is meant to build on Linux.
*Local builds on macos do not work (you don't have access to perf events).*

### Native linux

Install the required libraries described in the [Dockerfile](../app/base-env/Dockerfile).
Once all dependencies are installed, you can run the [Build Commands section](#build-commands).

### Docker

The [Dockerfile](../app/base-env/Dockerfile) contains all necessary dependencies to build the project.

```
./tools/launch_local_build.sh
```

Once inside the container, you can run the [Build Commands section](#build-commands).


## Build commands

### Building the native profiler

```bash
source setup_env.sh
MkBuildDir Rel
# For a release build
RelCMake ../
make -j 4 .
```

### Building the benchmark (collatz)

A bench application will be built by default. Following CMake flag controls the build decision: `-DBUILD_BENCHMARKS=ON`.

## Speeding up builds

### Bypassing the use of shared docker volumes on MacOS

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