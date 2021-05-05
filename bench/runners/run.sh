#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

DIR=$(git rev-parse --show-toplevel)
ASAN_SYMBOLIZER_PATH=$(which llvm-symbolizer)
CMD_BASE=${DIR}/release/ddprof
CMD=${CMD_BASE}

# Do service version stuff
VERFILE="tmp/run.ver"
mkdir -p $(dirname ${VERFILE})
VER=0
if [[ -f ${VERFILE} ]]; then VER=$(cat ${VERFILE}); fi
VER=$((VER+1))
echo ${VER} > ${VERFILE}

# Run it!
eval ${CMD} \
  -A ***REMOVED*** \
  -H intake.profile.datad0g.com \
  -P 80 \
  -S native-testservice${VER}\
  -E "test-staging" \
  "$@"
