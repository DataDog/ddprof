#!/usr/bin/env bash

# Test that the loader warns when a system-installed libdd_profiling-embedded.so
# has a version mismatch (USE_LOADER=ON only).  The mismatched library is still
# used (dlclose in a constructor is unsafe), but a warning must be printed.

set -euo pipefail

BUILDDIR="${PWD}"
EMBEDDED_LIB="${BUILDDIR}/libdd_profiling-embedded.so"
SHARED_LIB="${BUILDDIR}/libdd_profiling.so"

# Verify the version symbol is exported from the embedded lib
nm -D "${EMBEDDED_LIB}" | grep ddprof_profiling_version > /dev/null \
    || { echo "FAIL: ddprof_profiling_version not exported"; exit 1; }
echo "PASS: ddprof_profiling_version exported"

# Only test mismatch rejection in loader mode (USE_LOADER=ON)
if nm -D "${SHARED_LIB}" | grep ddprof_profiling_version > /dev/null; then
    echo "INFO: non-loader build — skipping mismatch rejection test"
    echo "PASS: all checks passed"
    exit 0
fi

echo "INFO: loader build — testing version mismatch rejection"

FAKE_LIB_DIR="${BUILDDIR}/test"
log="$(mktemp)"
trap 'rm -f "${log}"' EXIT

# The fake lib (built by cmake) is in the test dir. Put it on LD_LIBRARY_PATH
# so the loader's bare-name dlopen finds it instead of the real embedded lib.
LD_LIBRARY_PATH="${FAKE_LIB_DIR}" LD_PRELOAD="${SHARED_LIB}" \
    "${BUILDDIR}/test/simple_malloc-static" --loop 10 --spin 10 \
    > "${log}" 2>&1 || true

grep "version mismatch" "${log}" > /dev/null \
    || { echo "FAIL: no version mismatch warning"; cat "${log}"; exit 1; }
grep "0.0.0+wrong" "${log}" > /dev/null \
    || { echo "FAIL: wrong version not shown in warning"; cat "${log}"; exit 1; }
echo "PASS: version mismatch detected and reported"

echo "PASS: all checks passed"
