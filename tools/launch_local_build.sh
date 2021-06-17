#!/bin/bash

DEFAULT_BASE_NAME=base_ddprof

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

if [ -e ${TOP_LVL_DIR}/.env ]; then
    source ${TOP_LVL_DIR}/.env
fi

if [ -z  ${DEFAULT_DEV_WORKSPACE} ]; then
    DEFAULT_DEV_WORKSPACE=${TOP_LVL_DIR}
fi

# Check if base image exists
if [ -z "$(docker images |grep ${DEFAULT_BASE_NAME})" ]; then
    cd ./app/base-env
    echo "Building image"
    docker build -t ${DEFAULT_BASE_NAME} .
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
echo "Store changes in binded directory : /app"
docker run -it --rm -v /run/host-services/ssh-auth.sock:/ssh-agent --cap-add SYS_ADMIN -v ${DEFAULT_DEV_WORKSPACE}:/app -e SSH_AUTH_SOCK=/ssh-agent ${DEFAULT_BASE_NAME}:latest /bin/bash

exit 0
