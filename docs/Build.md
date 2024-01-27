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

### Running static analysis

cppcheck is run with a dedicated build target from within a build folder.
```bash
make cppcheck
```

Clang tidy checks require llvm 17 to be installed.
Using the build image will guarantee this.
```bash
CXX=clang++-17 CC=clang-17 source ./setup_env.sh
MkBuildDir ClangDeb
DebCMake -DENABLE_CLANG_TIDY=ON ../
```

### Updating libdatadog

Head over to the libdatadog, do your changes and copy the library back to your vendor directory.

```bash
cp ${workdir}/libdatadog/headers/include/datadog/common.h ${workdir}/ddprof/vendor_gcc_unknown-linux-2.35_Debug/libdatadog-v2.1.0/include/datadog/common.h
cp ${workdir}/libdatadog/headers/lib/libdatadog_profiling.a ${workdir}/ddprof/vendor_gcc_unknown-linux-2.35_Debug/libdatadog-v2.1.0/lib/
```
