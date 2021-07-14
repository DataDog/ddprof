#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

export ASAN_SYMBOLIZER_PATH=$(which llvm-symbolizer)
CMD_BASE=${TOP_LVL_DIR}/release/ddprof
CMD=${CMD_BASE}

# Do service version stuff
VERFILE="tmp/run.ver"
mkdir -p $(dirname ${VERFILE})
VER=0
if [[ -f ${VERFILE} ]]; then VER=$(cat ${VERFILE}); fi
VER=$((VER+1))
echo ${VER} > ${VERFILE}

DD_API_KEY=`$SCRIPTDIR/get_datad0g_key.sh`

# Run it!
eval ${CMD} \
  -A $DD_API_KEY \
  -H intake.profile.datad0g.com \
  -P 80 \
  -S native-testservice${VER}\
  -u 5 \
  -E "test-staging" \
  "$@"

exit 0
