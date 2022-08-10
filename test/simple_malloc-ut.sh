#!/usr/bin/env bash

set -euo pipefail

export DD_PROFILING_NATIVE_SHOW_SAMPLES=1
export DD_PROFILING_NATIVE_USE_EMBEDDED_LIB=1
export LD_LIBRARY_PATH=$PWD

opts=(--timeout 200)
log_file=$(mktemp -p "${PWD}" log.XXXX)

# Test disabled static lib mode
./test/simple_malloc-static "${opts[@]}" >"${log_file}"

if grep -qF "sample[" "${log_file}"; then
    echo "Unexpected samples"
    cat "${log_file}"
    exit 1
fi

# Test enabled static lib mode
./test/simple_malloc-static --profile "${opts[@]}" >"${log_file}"

if ! grep -q "sample\[type=cpu-samples.*;do_lot_of_allocations;" "${log_file}" || ! grep -q "sample\[type=alloc-samples.*;do_lot_of_allocations;" "${log_file}"; then
    echo "No samples"
    cat "${log_file}"
    exit 1
fi

# Test disabled shared lib mode
./test/simple_malloc-shared "${opts[@]}" >"${log_file}"

if grep -qF "sample[" "${log_file}"; then
    echo "Unexpected samples"
    cat "${log_file}"
    exit 1
fi

# Test enabled shared lib mode
./test/simple_malloc-shared --profile "${opts[@]}" >"${log_file}"

if ! grep -q "sample\[type=cpu-samples.*;do_lot_of_allocations;" "${log_file}" || ! grep -q "sample\[type=alloc-samples.*;do_lot_of_allocations;" "${log_file}"; then
    echo "No samples"
    cat "${log_file}"
    exit 1
fi

# Test wrapper mode
./ddprof -X no ./test/simple_malloc "${opts[@]}" >"${log_file}"
if ! grep -q "sample\[type=cpu-samples.*;do_lot_of_allocations;" "${log_file}" || ! grep -q "sample\[type=alloc-samples.*;do_lot_of_allocations;" "${log_file}"; then
    echo "No samples"
    cat "${log_file}"
    exit 1
fi

rm "${log_file}"
