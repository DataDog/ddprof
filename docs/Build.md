# ddprof build

We do not recommend for users to recompile the application. The pre-built binaries should be compatible with your system.
Checkout the release page for our latest builds.

## Environment setup

The dockerized environment will take care of installing all the dependencies.

### Native linux

Install the required libraries described in the [Dockerfile](../app/base-env/Dockerfile).
Once all dependencies are installed, you can run the [Build Commands section](#build-commands).

### Docker

The [Dockerfile](../app/base-env/Dockerfile) contains all necessary dependencies to build the project.
Here is a script that mounts the `ddprof` folder within the build container. 

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
