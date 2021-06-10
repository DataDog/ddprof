# ddprof build

## Environment setup

ddprof is meant to build on Linux.
*Local builds on macos do not work (you don't have access to perf events).*

### Virtual box

Install a VM with Ubuntu & build from there.
You will need your ssh keys configured to retrieve dependencies.
*Under construction* :building_construction:

### Docker

Docker can be used if you are not already on a linux environment. You need an ssh configuration as some repositories are private.
The following commands create a docker container based on ubuntu to build while using your current ssh configuration.

```bash
#Use the ubuntu CI with dependencies to build
cd ./app/base-env
docker build -t base_ddprof .
#Check if agent is runing (or add it if needed : ssh-add ~/.ssh/id_rsa):
ssh-add -L
# The container is cleared on exit : do not store things in it
docker run -it --rm -v /run/host-services/ssh-auth.sock:/ssh-agent -v ~/dd:/app -e SSH_AUTH_SOCK=/ssh-agent --name ddprof_build ddprof_base:latest /bin/bash
```

:warning: if you use worktrees you will have to mount extra folders (as the build uses `git rev-parse --short HEAD` to define version name).

### Native linux

Check the required libraries described in the [app/base-env/Dockerfile](app/base-env/Dockerfile).

## Build commands

### Make dependencies

```bash
make deps
```

### Make the project

```bash
make build
./release/ddprof --help
```

## Run commands

*Under construction* :building_construction:

### Setup

Write your datadog keys in a .env file in the root of the repository. For now only the DD_API_DATAD0G_KEY is necessary.

```bash
DD_API_PROD_KEY=<prod key>
DD_API_EU_KEY=<europe key (not used for now)>
DD_API_STAGING_KEY=<staging key>
DD_API_DATAD0G_KEY=<datad0g key>
```

### Run examples

A stress test examples is available (you need to build the bench/collatz folder first)

```bash
./bench/runners/runit.sh collatz 
```

### Troubleshooting

Can you reach the intake service ? Check if you get a 400 error code. Check it also from the docker container.

```bash
curl -XPOST -i https://intake.profile.datad0g.com/v1/input
```

## Artifacts

You can always check out the available `ddprof` and `collatz` binaries with something like:

```bash
aws-vault exec build-stable-developer -- aws s3 ls s3://binaries.ddbuild.io/ddprof/release/
```

### Base Image

**where**: 486234852809.dkr.ecr.us-east-1.amazonaws.com/ci/ddprof:base
**how**: app/base-env
**when**: manual job in CI

This image contains the system-level requirements for building `ddprof`.

### Build Image

**where**: 486234852809.dkr.ecr.us-east-1.amazonaws.com/ci/ddprof:build
**how**: app/base-env
**when**: manual job in CI

This image layers the base, adding intermediate build artifacts from dependencies.  Since `ddprof` has a one-shot build system, the artifacts represent code and build resources rather than binary objects.  Dependencies are currently only `elfutils` and `libddprof`.

### ddprof

**where**: binaries.ddbuild.io/ddprof/release/ddprof
**how**: `make build`
**when*: automatic job in CI, executing when `branch == main`

This is the native profiler.  It is shipped as a binary.

### collatz

**where**: binaries.ddbuild.io/ddprof/release/collatz
**how**: `make bench`
**when**: manual job in CI

This is a benchmarking tool for the native profiler.

## Versioning

`ddprof` and `collatz` are versioned in binaries.ddbuild.io with something like the following scheme.

* `ddprof/release/ddprof`: bleeding edge release
* `ddprof/release/ddprof_X.Y.X`: last passing build for a given patch version
* `ddprof/release/ddprof_X.Y.Z_CIID-SHORTHASH`: pinned build, where CIID used to be the IID for CI, but is now the ID for quick reference.
* `ddprof/release/ddprof_X.Y.Z_rcN`: release candidate (not yet implemented, may never be used)
* `ddprof/release/ddprof_X.Y.Z_final`: release build (not yet implemented)

`ddprof` and `collatz` will always report their own most verbose version information.  Release builds are generated manually (actually, not at all right now).  Information is injected during the build process, so the executables can always report `tool --version` correctly.  When uploaded back to S3, `tool --version` is used rather than CI/git metadata; hopefully the slight inefficiency persists a strong binding between the various forms of metadata.
