# ddprof build

## Environment

ddprof is meant to build on Linux.
*Local builds on macos do not work.*

### Virtual box

Install a VM with Ubuntu & build from there.
You will need your ssh keys configured to retrieve dependencies.
*Under construction* :building_construction:

### Docker

Docker can be used to build from Mac-OS. For now we need an ssh configuration as some repositories are private.
Warning : do not push this image or you will compromise your ssh keys. LOCAL USAGE ONLY.

```bash
cd ./app/base-env
docker build -t base . # having another base image might clash
cd ../build-local
docker build -t local_builder --build-arg "SSH_PRIVATE_KEY=`cat ~/.ssh/id_rsa`" --build-arg "SSH_PUBLIC_KEY=`cat ~/.ssh/id_rsa.pub`" --build-arg "SSH_KNOWN_HOSTS=`cat ~/.ssh/known_hosts`" .
```

Run the built container. You can share a directory to edit from macos and build inside docker.
Attach to the container with /bin/bash.
You can run build or test commands from this terminal. You will need to enter your ssh passphrases for the first build.

*Under construction : simplify* :building_construction:

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
