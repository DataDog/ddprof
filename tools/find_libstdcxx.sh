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
GCC=$(command -v gcc{-12,-11,-10,-9,-8,} | head -n 1)
if [ ! -z ${1:-} ]; then
  if command -v ${1} > /dev/null; then
  GCC=${1}
  fi
fi
if [ -z ${GCC:-} ]; then
  echo "No suitable GCC found or given" >&2
  GCC=gcc
fi

echo $(dirname $(${GCC} -print-libgcc-file-name))/libstdc++.a
