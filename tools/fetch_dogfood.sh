#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

usage() {
    echo "Usage :"
    echo "$0 <tag/branch> <path_to_vendor_dir>"
    echo ""
    echo "Example"
    echo "  $0 v2.0 ./vendor"
}

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

cd $2
git clone --depth 1 --branch $1 https://github.com/garrettsickles/DogFood.git  2> /dev/null
