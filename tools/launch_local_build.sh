#!/bin/bash

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
    echo " You can specify a foder to mount in a .env file ${TOP_LVL_DIR}/.env"
    echo "    Add this line to the file --> DEFAULT_DEV_WORKSPACE=<some path you want to mount>"
    echo ""
    echo " Optional parameters "
    echo "    --test/-t : launch the test image."
    echo "    --clean/-c : rebuild the image before creating it."
}

if [ $# != 0 ] && [ $1 == "-h" ]; then
    usage
    exit 0
fi

PERFORM_CLEAN=0
while [ $# != 0 ]; do 
    if [ $# != 0 ] && [ $1 == "-t" -o $1 == "--test" ]; then
        DEFAULT_BASE_NAME=test_ddprof
        BASE_DOCKERFILE="./app/test-env/Dockerfile"
        shift
        continue
    fi

    if [ $# != 0 ] && [ $1 == "--clean" -o $1 == "-c" ]; then
        PERFORM_CLEAN=1
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

echo "Considering docker image    : $DEFAULT_BASE_NAME"
echo "              Built from    : $BASE_DOCKERFILE"
echo "           Mounting to /app : ${DEFAULT_DEV_WORKSPACE}"


if [ $PERFORM_CLEAN -eq 1 ]; then
    echo "Clean image : ${DEFAULT_BASE_NAME}"
    docker image rm ${DEFAULT_BASE_NAME}
fi


# Check if base image exists
if [ -z "$(docker images |grep ${DEFAULT_BASE_NAME})" ]; then
    echo "Building image"
    docker build -t ${DEFAULT_BASE_NAME} -f $BASE_DOCKERFILE .
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
echo "Binded directory : /app  <--> ${DEFAULT_DEV_WORKSPACE}"
docker run -it --rm -v /run/host-services/ssh-auth.sock:/ssh-agent -w /app --cap-add SYS_ADMIN -v ${DEFAULT_DEV_WORKSPACE}:/app -e SSH_AUTH_SOCK=/ssh-agent ${DEFAULT_BASE_NAME}:latest /bin/bash

exit 0
