#!/bin/bash
set -euo pipefail
IFS=$'\n\t'

# Figure out latest clang-format command.  This might be different than the
# user's `clang-format` if they have one, but that's the point.
# TODO fix copypasta from other tools  
CLANG_FORMAT=""
if   command -v clang-format-12 >/dev/null 2>&1; then CLANG_FORMAT=clang-format-12
elif command -v clang-format-11 >/dev/null 2>&1; then CLANG_FORMAT=clang-format-11
elif command -v clang-format-10 >/dev/null 2>&1; then CLANG_FORMAT=clang-format-10
elif command -v clang-format-9  >/dev/null 2>&1; then CLANG_FORMAT=clang-format-9
else echo "No suitable clang-format found." && exit 1
fi

${CLANG_FORMAT} -style=file -i src/*.[ch] include/*.h
