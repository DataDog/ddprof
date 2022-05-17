#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

# Find most recent clang-format, defaulting to an unqualified default
CLANG_FORMAT=$(command -v clang-format{-13,-12,-11,-10,-9,} | head -n 1)
if ! command -v "${CLANG_FORMAT}" > /dev/null 2>&1; then
  echo "No suitable clang-format found"
  exit 1
fi

# Process arguments
[[ -z "${APPLY:-}" ]] && APPLY="no"
[[ "${1:-,,}" == "apply" ]] && APPLY="yes"

CLANG_OPTION="--dry-run --Werror"
if [ ${APPLY:-,,} == yes ];then 
  #inplace
  CLANG_OPTION="-i"
fi

declare -a arr_folders=("src" "test" "include")

FILES_TO_FORMAT=".*[.]\(cpp\|cc\|c\|cxx\|h\|hpp\)"
find "${arr_folders[@]}" -regex "${FILES_TO_FORMAT}" -print0 | xargs -0 "${CLANG_FORMAT}" ${CLANG_OPTION}
