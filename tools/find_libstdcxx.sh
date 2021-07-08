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
RETVAL=""
for c in $(command -v gcc{-12,-11,-10,-9,-8,}); do
  LIBSTDCXX_FILE=$(dirname $($c -print-libgcc-file-name))/libstdc++.a
  if [ -f "$LIBSTDCXX_FILE" ]; then
    RETVAL="$LIBSTDCXX_FILE"
    break;
  fi
done
if [ ! -z ${1:-} ]; then
  if command -v ${1} > /dev/null; then
    LIBSTDCXX_FILE=$(dirname $(${1} -print-libgcc-file-name))/libstdc++.a
    if [ -f "$LIBSTDCXX_FILE" ]; then
      RETVAL="$LIBSTDCXX_FILE"
      break;
    fi
  fi
fi
if [ -z ${RETVAL} ]; then
  echo "No suitable libstdc++.a found"
fi

echo $RETVAL
