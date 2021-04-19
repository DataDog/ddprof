#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

DIR=$(git rev-parse --show-toplevel)
ASAN_SYMBOLIZER_PATH=$(which llvm-symbolizer)
CMD_BASE=${DIR}/release/ddprof
CMD=${CMD_BASE}
JOB="./redis-runner.sh"
for arg in "$@"; do
  if [[ ${arg} == "debug" ]]; then CMD="gdb -ex run -ex 'set follow-fork-mode child' -ex 'set print pretty on' --args ${CMD_BASE}"; fi
  if [[ ${arg} == "strace" ]]; then CMD="strace -f -o /tmp/test.out -s 2500 -v ${CMD_BASE}"; fi
  if [[ ${arg} == "redis" ]]; then JOB="./redis-runner.sh"; fi
  if [[ ${arg} == "collatz" ]]; then JOB="./collrunner.sh"; fi
done

# Do service version stuff
VERFILE="tmp/runner.ver"
mkdir -p $(dirname ${VERFILE})
VER=0
if [[ -f ${VERFILE} ]]; then VER=$(cat ${VERFILE}); fi
VER=$((VER+1))
echo ${VER} > ${VERFILE}

# Run it!
${CMD} \
  -A ***REMOVED*** \
  -H intake.profile.datad0g.com \
  -P 80 \
  -S native-testservice${VER}\
  -E "test-staging" \
  -u 10.0 \
  -e sCPU \
  -e sCI \
  -e kBLKS \
  ${DIR}/bench/runners/${JOB}
