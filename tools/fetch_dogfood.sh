#!/bin/bash
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
