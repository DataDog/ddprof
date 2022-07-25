#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

set -euo pipefail

DEFAULT_BASE_NAME=base_ddprof
BASE_DOCKERFILE="./app/base-env/Dockerfile"

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname "$SCRIPTPATH")
cd "$SCRIPTDIR/../"
TOP_LVL_DIR=$PWD
cd "$CURRENTDIR"

usage() {
    echo "$0 [-t] [clean]"
    echo "Launch a docker instance for local developments. Your ssh socket is forwarded to the build image."
    echo ""
    echo "Example launching the ddprof ubuntu docker for build purposes"
    echo ""
    echo "${0} -u 18"
    echo ""
    echo " Shared files" 
    echo " Recommended solution -- Use a docker-sync.yml file in the directory from which you launch."
    echo " Alternative solution -- You can specify a folder to mount in a .env file ${CURRENTDIR}/.env"
    echo " Default solution     -- if nothing is specified current directory will be shared (!slow on macos)"    
    echo ""
    echo " Optional parameters "
    echo "    --dockerfile/-f : use a custom docker file."
    echo "    --clean/-c : rebuild the image before creating it."
    echo "    --ubuntu_version/-u : specify ubuntu version (expected values: 16 / 18 / 20)"
    echo "    --image_id/-i : use a specified docker ID, conflicts with -u."
    echo "    --clang : use clang instead of gcc."
}

if [ $# != 0 ] && [ "$1" == "-h" ]; then
    usage
    exit 0
fi

PERFORM_CLEAN=0
UBUNTU_VERSION=18
COMPILER="gcc"

while [ $# != 0 ]; do 
    case $1 in
        -f|--dockerfile)
            # Use md5 of file to identify a unique name
            DEFAULT_BASE_NAME=$(md5sum $2 | awk '{print $1}')
            BASE_DOCKERFILE="$2"
            shift
            shift
            ;;
        --clean|-c)
            PERFORM_CLEAN=1
            shift
            ;;
        --ubuntu_version|-u)
            UBUNTU_VERSION=$2
            shift
            shift
            ;;
        --image_id|-i)
            DOCKER_NAME=$2
            DOCKER_TAG=""
            CUSTOM_ID="yes"
            shift
            shift
            ;;
        --clang)
            COMPILER="clang"
            shift
            ;;
        *)
            echo "Error : unhandled parameter"
            usage
            exit 1
    esac
done

if [ -e "${CURRENTDIR}/.env" ]; then
    source "${CURRENTDIR}/.env"
fi

MOUNT_CMD="-v ${DEFAULT_DEV_WORKSPACE:-${CURRENTDIR}}:/app"

# Support docker sync : Improves compilation speed 
# Example of config (to be pasted in the docker-sync.yml file)
# version: "2"
# syncs:
#   ddprof-sync:
#     sync_strategy: "native_osx"
#     src: "./"
#     host_disk_mount_mode: "cached"
if [ -e "${CURRENTDIR}/docker-sync.yml" ]; then
    # Grep the docker sync config (expected in root directory) to retrieve name of volume
    VOLUME_SYNC=$(grep -A 1 "syncs:" "${CURRENTDIR}/docker-sync.yml" | tail -n 1 | awk -F ':' '{print $1}' | sed "s/ //g")
    echo "$VOLUME_SYNC"
    MOUNT_CMD="--mount source=${VOLUME_SYNC},target=/app"
    if ! docker-sync list | grep -q "$VOLUME_SYNC"; then
        echo "Please generate a volume: $VOLUME_SYNC"
        echo "Suggested commands:"
        echo "docker volume create $VOLUME_SYNC"
        echo "docker-sync start"
        exit 1
    fi
fi

# If we didn't pass a custom ID, then focus on Ubuntu
if [ ! ${CUSTOM_ID:-,,} == "yes" ]; then
    DOCKER_NAME=${DEFAULT_BASE_NAME}_${UBUNTU_VERSION}_${COMPILER}
    DOCKER_TAG=":latest"
fi

echo "Considering docker image    : $DOCKER_NAME"
echo "              Built from    : $BASE_DOCKERFILE"
echo "           Mount command    : ${MOUNT_CMD}"

if [ $PERFORM_CLEAN -eq 1 ]; then
    echo "Clean image : ${DOCKER_NAME}"
    # if docker image does not exist, we should not fail
    docker image rm "${DOCKER_NAME}" || true
fi

# Check if base image exists
if [ ! ${CUSTOM_ID:-,,} == "yes" ] && ! docker images | awk '{print $1}'| grep -qE "^${DOCKER_NAME}$"; then
    echo "Building image"
    BUILD_CMD="docker build -t ${DOCKER_NAME} --build-arg COMPILER=$COMPILER --build-arg UBUNTU_VERSION=${UBUNTU_VERSION} -f $BASE_DOCKERFILE ."
    #echo "${BUILD_CMD}"
    eval "${BUILD_CMD}"
else 
    echo "Base image found, not rebuilding. Remove it to force rebuild."
fi

if [[ $OSTYPE == darwin* ]]; then
  if [ -z "$(ssh-add -L)" ]; then
      echo "Please start your ssh agent. Example :"
      echo "ssh-add ~/.ssh/id_rsa"
      exit 1
  fi
  echo "Launch docker image, DO NOT STORE ANYTHING outside of mounted directory (container is erased on exit)."
  # shellcheck disable=SC2086
  MOUNT_SSH_AGENT="-v /run/host-services/ssh-auth.sock:/ssh-agent -e SSH_AUTH_SOCK=/ssh-agent"
else
  if [[ $OSTYPE != darwin* ]]; then
      echo "Script mainly configured for MacOS."
      echo "Not mounting ssh agent"
  fi
  MOUNT_SSH_AGENT=""
fi

CMD="docker run -it --rm  -w /app ${MOUNT_SSH_AGENT} --cap-add CAP_SYS_PTRACE --cap-add SYS_ADMIN ${MOUNT_CMD} \"${DOCKER_NAME}${DOCKER_TAG}\" /bin/bash"

eval "$CMD"
