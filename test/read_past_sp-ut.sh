#!/usr/bin/env bash

set -euo pipefail

# leak sanitizer triggers spawn of a new thread, that causes incomplete stacks
export LSAN_OPTIONS=detect_leaks=0

./ddprof -l debug -X no -O test_ -e sCPU ./test/read_past_sp > log
# need to wait for the profiling process to terminate, otherwise log might be incomplete
p=$(grep -o "Created child.*" log | awk '{print $3}')
while kill -0 "$p" 2> /dev/null; do sleep 0.05s; done

v=$(grep "datadog[.]profiling[.]native[.]unwind[.]stack[.]incomplete: *[0-9]*" -o log | awk -F: '{print $2}')
if [[ -z "$v" ]]; then
    echo "Could not find metrics"
    exit 1
# allow at most 3 incomplete stacks
elif [[ "$v" -gt 3 ]]; then
    echo "Found incomplete stacks"
    exit 1
fi
