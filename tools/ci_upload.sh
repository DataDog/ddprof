#!/bin/bash
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
  # Upload with ddprof_version : example ddprof_0.6.4
  $DIR/tools/upload.sh -p ${S3ROOT}/release -f ${RELEASEBIN} -n $(${RELEASEBIN} --version | sed -e 's/ /_/g' -e 's/\+.*//g')
  if [ ! -z $PROMOTE ] && [ $PROMOTE = "MAJOR" ]; then
    # Upload with ddprof : warning this will be pulled down automatically in Datadog
    $DIR/tools/upload.sh -p ${S3ROOT}/release -f ${RELEASEBIN} -n $(${RELEASEBIN} --version | sed -e 's/ .*//g')
  fi
fi

if [ ! -z "${DEBUGBIN}" ]; then
  $DIR/tools/upload.sh -p ${S3ROOT}/debug -f ${DEBUGBIN} -n $(${DEBUGBIN} --version | sed -e 's/ /_/g' -e 's/\+/_/g')
  $DIR/tools/upload.sh -p ${S3ROOT}/debug -f ${DEBUGBIN} -n $(${DEBUGBIN} --version | sed -e 's/ /_/g' -e 's/\+.*//g')
fi
