#!/usr/bin/env bash

set -euo pipefail

export DD_PROFILING_EXPORT=no
export DD_PROFILING_NATIVE_SHOW_SAMPLES=1
export DD_PROFILING_NATIVE_USE_EMBEDDED_LIB=1
export DD_PROFILING_NATIVE_LOG_LEVEL=notice
export LD_LIBRARY_PATH=$PWD

opts="--loop 1000 --spin 100"
log_file=$(mktemp "${PWD}/log.XXXXXX")

check() {
    cmd="$1"
    expect_samples="$2"
    ${cmd} >"${log_file}"
    if [ "${expect_samples}" -eq 1 ]; then
        # need to wait for the profiling process to terminate, otherwise log might be incomplete
        p=$(grep -o "Created child.*" "${log_file}" | awk '{print $3}')
        while kill -0 "$p" 2>/dev/null; do sleep 0.05s; done
    fi

    if [ "${expect_samples}" -eq 1 ]; then
        if ! grep -q "sample\[type=cpu-samples.*;do_lot_of_allocations" "${log_file}" || ! grep -q "sample\[type=alloc-samples.*;do_lot_of_allocations" "${log_file}"; then
            echo "No sample found"
            cat "${log_file}"
            exit 1
        fi
    else
        if grep -qF "sample[" "${log_file}"; then
            echo "Unexpected samples"
            cat "${log_file}"
            exit 1
        fi
    fi
}

# Test disabled static lib mode
check "./test/simple_malloc-static ${opts}" 0

# Test enabled static lib mode
check "./test/simple_malloc-static --profile ${opts}" 1

# Test disabled shared lib mode
check "./test/simple_malloc-shared ${opts}" 0

# Test enabled shared lib mode
check "./test/simple_malloc-shared --profile ${opts}" 1

# Test wrapper mode
check "./ddprof -X no ./test/simple_malloc ${opts}" 1

rm "${log_file}"
