# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

#!/bin/bash

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

## Figure out the configured build folder.  This is a copy/paste job from run.sh
source ${TOP_LVL_DIR}/tools/find_build.sh
BUILD_FOLDER=$(find_build)
if [ -z ${BUILD_FOLDER} ]; then
  echo "Error, no BUILD_FOLDER variable defined"
  exit -1
fi

## Actually run collatz
while true; do
  ${BUILD_FOLDER}/bench/collatz/collatz 2 200000 500000 A
  ${BUILD_FOLDER}/bench/collatz/collatz 2 200000 500000 A
done
