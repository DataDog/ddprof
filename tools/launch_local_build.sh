#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

DEFAULT_BASE_NAME=base_ddprof
BASE_DOCKERFILE="./app/base-env/Dockerfile"

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

usage() {
    echo "$0 [-t] [clean]"
    echo "Launch a docker instance locally of the CI images. ssh socket is forwarded to the build image."
    echo " You can specify a folder to mount in a .env file ${TOP_LVL_DIR}/.env"
    echo "    Add this line to the file --> DEFAULT_DEV_WORKSPACE=<some path you want to mount>"
    echo "You can have a docker-sync.yml file in the root of this repo which will be used as a mounted volume (speeds up compile)."
    echo ""
    echo " Optional parameters "
    echo "    --test/-t : launch the test image."
    echo "    --centos/-C : launch the test image."
    echo "    --clean/-c : rebuild the image before creating it."
    echo "    --ubuntu_version/-u : use a given ubuntu version."
}

if [ $# != 0 ] && [ $1 == "-h" ]; then
    usage
    exit 0
fi

PERFORM_CLEAN=0
UBUNTU_VERSION=18

while [ $# != 0 ]; do 
    if [ $# != 0 ] && [ $1 == "-t" -o $1 == "--test" ]; then
        DEFAULT_BASE_NAME=test_ddprof
        BASE_DOCKERFILE="./app/test-env/Dockerfile"
        shift
        continue
    fi
    if [ $# != 0 ] && [ $1 == "-C" -o $1 == "--centos" ]; then
        DEFAULT_BASE_NAME=test_centos
        BASE_DOCKERFILE="./app/centos/Dockerfile"
        shift
        continue
    fi
    if [ $# != 0 ] && [ $1 == "--clean" -o $1 == "-c" ]; then
        PERFORM_CLEAN=1
        shift
        continue
    fi
    if [ $# != 0 ] && [ $1 == "--ubuntu_version" -o $1 == "-u" ]; then
        UBUNTU_VERSION=$2
        shift
        shift
        continue
    fi

    echo "Error : unhandled parameter"
    usage
    exit 1
done

if [ -e ${TOP_LVL_DIR}/.env ]; then
    source ${TOP_LVL_DIR}/.env
fi

if [ -z  ${DEFAULT_DEV_WORKSPACE} ]; then
    DEFAULT_DEV_WORKSPACE=${TOP_LVL_DIR}
fi

MOUNT_CMD="-v ${DEFAULT_DEV_WORKSPACE}:/app"

# Support docker sync : Improves compilation speed 
# Example of config (to be pasted in the docker-sync.yml file)
# version: "2"
# syncs:
#   ddprof-sync:
#     sync_strategy: "native_osx"
#     src: "./"
#     host_disk_mount_mode: "cached"
if [ -e "${TOP_LVL_DIR}/docker-sync.yml" ]; then
    # Grep the docker sync config (expected in root directory) to retrieve name of volume
    VOLUME_SYNC=$(grep -A 1 "syncs:" ${TOP_LVL_DIR}/docker-sync.yml | tail -n 1 | awk -F ':' '{print $1}' | sed "s/ //g")
    echo $VOLUME_SYNC
    MOUNT_CMD="--mount source=${VOLUME_SYNC},target=/app"
    if [ -z "$(docker-sync list | grep $VOLUME_SYNC)" ]; then
        echo "Please generate a volume: $VOLUME_SYNC"
        echo "Suggested commands:"
        echo "docker volume create $VOLUME_SYNC"
        echo "docker-sync start"
        exit 1
    fi
fi

DOCKER_TAG=${DEFAULT_BASE_NAME}_${UBUNTU_VERSION}

echo "Considering docker image    : $DOCKER_TAG"
echo "              Built from    : $BASE_DOCKERFILE"
echo "           Mount command    : ${MOUNT_CMD}"

if [ $PERFORM_CLEAN -eq 1 ]; then
    echo "Clean image : ${DOCKER_TAG}"
    docker image rm ${DOCKER_TAG}
fi

# Check if base image exists
if [ -z "$(docker images |grep ${DOCKER_TAG})" ]; then
    echo "Building image"
    # docker build --build-arg arg=2.3
    docker build -t ${DOCKER_TAG} -f $BASE_DOCKERFILE --build-arg UBUNTU_VERSION=${UBUNTU_VERSION} .
    cd -
else 
    echo "Base image found, not rebuilding. Remove it to force rebuild."
fi

if [ -z "$(ssh-add -L)" ]; then
    echo "Please start your ssh agent. Example :"
    echo "ssh-add ~/.ssh/id_rsa"
    exit 1
fi

if [ -z `echo $OSTYPE|grep darwin` ]; then
    echo "Script only tested on MacOS."
    echo "Attempting to continue : please update script for your OS if ssh socket is failing."
fi 

echo "Launch docker image, DO NOT STORE ANYTHING outside of mounted directory (container is erased on exit)."
docker run -it --rm -v /run/host-services/ssh-auth.sock:/ssh-agent -w /app --cap-add CAP_SYS_PTRACE --cap-add SYS_ADMIN ${MOUNT_CMD} -e SSH_AUTH_SOCK=/ssh-agent ${DOCKER_TAG}:latest /bin/bash

exit 0
