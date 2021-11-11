#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
# set -euo pipefail
# IFS=$'\n\t'

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

# Find most recent clang-format, defaulting to an unqualified default
CLANG_FORMAT=$(command -v clang-format{-13,-12,-11,-10,-9,} | head -n 1)
if ! command -v "${CLANG_FORMAT}" > /dev/null 2>&1; then
  echo "No suitable clang-format found"
  exit 1
fi

# Process arguments
RC=0
[[ -z "${APPLY:-}" ]] && APPLY="no"
[[ "${1:-,,}" == "apply" ]] && APPLY="yes"

# Setup a tmpfile 
tmpfile=$(mktemp /tmp/clang-format-diff.XXXXXX)

CLANG_OPTION="--dry-run"
if [ ${APPLY,,} == yes ];then 
  #inplace
  CLANG_OPTION="-i"
fi

declare -a arr_folders=("src" "test" "include")

FILES_TO_FORMAT="*.cpp *.cc *.c *.cxx *.h *.hpp"

for folder in "${arr_folders[@]}"
do
  cd ${TOP_LVL_DIR}/${folder}
  echo "### Applying to : $PWD ###"
  ${CLANG_FORMAT} ${CLANG_OPTION} ${FILES_TO_FORMAT} &> ${tmpfile}
  NB_LINES=$(cat ${tmpfile} | grep -v "No such file or directory" | wc -l)
  if [ ${NB_LINES} -gt 1 ]; then
    RC=1
  fi
  cat ${tmpfile} | grep -v "No such file or directory"
done

exit $RC
