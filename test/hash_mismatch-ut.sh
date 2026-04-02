#!/usr/bin/env bash

# Test that the loader rejects an installed libdd_profiling-embedded.so whose
# SHA-256 does not match the build-time hash (USE_LOADER=ON only).
# The loader should fall back to the embedded copy and print a warning.

set -euo pipefail

BUILDDIR="${PWD}"
EMBEDDED_LIB="${BUILDDIR}/libdd_profiling-embedded.so"
SHARED_LIB="${BUILDDIR}/libdd_profiling.so"

# Detect loader build: in loader builds, the shared lib does NOT directly
# export ddprof_start_profiling as a defined (T) symbol from dd_profiling.cc;
# instead it has a thin forwarding stub from loader.c.  A reliable proxy is
# checking whether the shared lib contains the embedded lib name string.
if ! strings "${SHARED_LIB}" | grep -q "libdd_profiling-embedded.so" 2>/dev/null; then
    echo "INFO: non-loader build — skipping hash mismatch test"
    echo "PASS: all checks passed"
    exit 0
fi

echo "INFO: loader build — testing hash mismatch rejection"

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT

# Create a fake libdd_profiling-embedded.so with wrong contents.
# A truncated copy is enough — the SHA-256 will not match.
head -c 1024 "${EMBEDDED_LIB}" > "${tmpdir}/libdd_profiling-embedded.so"

# Also place ddprof exe and the loader in the same directory so the loader
# finds both via dladdr-relative lookup.
cp "${SHARED_LIB}" "${tmpdir}/libdd_profiling.so"
printf '#!/bin/sh\nexit 0\n' > "${tmpdir}/ddprof"
chmod +x "${tmpdir}/ddprof"

log="$(mktemp)"
trap 'rm -rf "${tmpdir}" "${log}"' EXIT

# LD_PRELOAD the loader from the temp dir.  The fake embedded lib sits next
# to it, so the loader's find_installed_profiling_lib() will find it but the
# hash should not match.
LD_PRELOAD="${tmpdir}/libdd_profiling.so" \
    "${BUILDDIR}/test/simple_malloc-static" --loop 10 --spin 10 \
    > "${log}" 2>&1 || true

grep -q "hash mismatch" "${log}" \
    || { echo "FAIL: no hash mismatch warning"; cat "${log}"; exit 1; }
echo "PASS: hash mismatch detected and reported"

echo "PASS: all checks passed"
