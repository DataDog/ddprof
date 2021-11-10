#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

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

# We need to make sure this is set if we want ASAN to return symbol names
# instead of VM addresses
if [[ -z "${ASAN_SYMBOLIZER_PATH:-}" ]]; then
  if command -v llvm-symbolizer; then
    export ASAN_SYMBOLIZER_PATH=$(which llvm-symbolizer)
  else
    export ASAN_SYMBOLIZER_PATH=""
  fi
fi

# Overrides
if [[ -z "${CMD_BASE:-}" ]]; then CMD_BASE=${SCRIPTDIR}/run.sh; fi
CMD=${CMD_BASE}

JOB="redis-runner.sh"
DEFAULT_JOB_PATH="${TOP_LVL_DIR}/bench/runners/"

# Check for parameters (order matters)
for arg in "$@"; do
  # first run.sh params
  if [[ ${arg} == "jemalloc" ]]; then CMD="${CMD} -j 26 20"; fi
  # Then ddprof params
  if [[ ${arg} == "global" ]]; then CMD="${CMD} -g yes"; fi
  # Analysis tooling
  if [[ ${arg} == "debug" ]]; then CMD="gdb -ex run -ex 'set follow-fork-mode child' -ex 'set print pretty on' --args ${CMD}"; fi
  if [[ ${arg} == "strace" ]]; then CMD="strace -f -o /tmp/test.out -s 2500 -v ${CMD}"; fi
  if [[ ${arg} == "network" ]]; then CMD="strace -etrace=%network -f -o /tmp/test.out -s 2500 -v ${CMD}"; fi
  if [[ ${arg} == "ltrace" ]]; then CMD="ltrace -f -o /tmp/test.out -s 2500 -n 2 -x '*' -e malloc+free ${CMD}"; fi
  # Jobs and toy apps to run 
  if [[ ${arg} == "boggle" ]]; then JOB="BadBoggleSolver_run 1000"; fi #1000 seconds
  if [[ ${arg} == "envoy" ]]; then JOB="${DEFAULT_JOB_PATH}envoy-runner.sh"; fi
  if [[ ${arg} == "redis" ]]; then JOB="${DEFAULT_JOB_PATH}redis-runner.sh"; fi
  if [[ ${arg} == "compile" ]]; then JOB="${DEFAULT_JOB_PATH}compile-runner.sh"; fi
  if [[ ${arg} == "collatz" ]]; then JOB="${DEFAULT_JOB_PATH}collrunner.sh"; fi
  if [[ ${arg} == "sleep" ]]; then JOB="${DEFAULT_JOB_PATH}sleep.sh"; fi
  if [[ ${arg} == "noexist" ]]; then JOB="${DEFAULT_JOB_PATH}fakejob.sh"; fi
  if [[ ${arg} == "noexec" ]]; then JOB="${DEFAULT_JOB_PATH}non_executable_job.sh"; fi
done

# Do service version stuff
VERFILE="${TOP_LVL_DIR}/tmp/runner.ver"
mkdir -p $(dirname ${VERFILE})
VER=0
if [[ -f ${VERFILE} ]]; then VER=$(cat ${VERFILE}); fi
VER=$((VER+1))
echo ${VER} > ${VERFILE}

rm -rf debuglog.out

echo "${CMD} ${JOB}"
echo "##################################################"
eval ${CMD} ${JOB}

exit 0
