#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

# This script takes an executable and, presuming it ships a version string of
# the correct format and the right environment variables are in place, shoves
# it into S3
DIR=$(git rev-parse --show-toplevel)

# This script strongly assumes that the binary is executable and ships a version
# string of the exactly correct format
# Also rather strongly assumes this is running in CI... sorry!

if [ ! -z "${RELEASEBIN}" ]; then 
  # Upload with ddprof_version_buildid : example ddprof_0.6.4_5769351-8471af37
  $DIR/tools/upload.sh -p ${S3ROOT}/release -f ${RELEASEBIN} -n $(${RELEASEBIN} --version | sed -e 's/ /_/g' -e 's/\+/_/g')
  if [ ! -z $PROMOTE ] && [ $PROMOTE = "MAIN" ]; then
    $DIR/tools/upload.sh -p ${S3ROOT}/release -f ${RELEASEBIN} -n $(${RELEASEBIN} --version | sed -e 's/ .*//g')_main
  fi
  if [ ! -z $PROMOTE ] && [ $PROMOTE = "CANDIDATE" ]; then
    # Upload with ddprof_candidate
    $DIR/tools/upload.sh -p ${S3ROOT}/release -f ${RELEASEBIN} -n $(${RELEASEBIN} --version | sed -e 's/ .*//g')_candidate
  fi
  if [ ! -z $PROMOTE ] && [ $PROMOTE = "MAJOR" ]; then
    # Upload with ddprof : warning this will be pulled down automatically in Datadog
    $DIR/tools/upload.sh -p ${S3ROOT}/release -f ${RELEASEBIN} -n $(${RELEASEBIN} --version | sed -e 's/ .*//g')
  fi
fi

if [ ! -z "${DEBUGBIN}" ]; then
  $DIR/tools/upload.sh -p ${S3ROOT}/debug -f ${DEBUGBIN} -n $(${DEBUGBIN} --version | sed -e 's/ /_/g' -e 's/\+/_/g')
  $DIR/tools/upload.sh -p ${S3ROOT}/debug -f ${DEBUGBIN} -n $(${DEBUGBIN} --version | sed -e 's/ /_/g' -e 's/\+.*//g')
fi
