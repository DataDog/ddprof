#!/usr/bin/env bash

set -euo pipefail
./ddprof -l notice -X no -e sCPU ./test/read_past_sp > log
if [ "$(grep "datadog[.]profiling[.]native[.]unwind[.]stack[.]incomplete: *[0-9]*" -o log | awk -F: '{print $2}')" -ne 0 ]; then
    echo "Found incomplete stacks"
    exit 1
fi