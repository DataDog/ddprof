#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

# This should always point to the location of libstdc++ specified by gcc.
# It's possible that gcc or the passed parameter doesn't exist, but for clarity
# we want errors to be thrown right here.
GCC=gcc
if [ ! -z ${1:-} ]; then
  GCC=${1}
fi

echo $(dirname $(${GCC} -print-libgcc-file-name))/libstdc++.a
