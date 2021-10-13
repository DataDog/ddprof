#!/bin/bash

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

cd ${TOP_LVL_DIR}
source ./setup_env.sh
mkdir build_codeql
cd build_codeql
RelCMake ../
echo "Building from $PWD"
make -j 4
echo "finished building..."
