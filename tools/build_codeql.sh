# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

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
