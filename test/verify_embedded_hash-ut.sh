#!/usr/bin/env bash

# Verify that the SHA-256 in libdd_profiling-embedded_hash.h matches the
# actual libdd_profiling-embedded.so in the build directory.
# This catches stale hashes and stripping-induced mismatches.

set -euo pipefail

BUILDDIR="${PWD}"
HASH_HEADER="${BUILDDIR}/libdd_profiling-embedded_hash.h"
EMBEDDED_LIB="${BUILDDIR}/libdd_profiling-embedded.so"

expected=$(grep -oP '"[0-9a-f]{64}"' "${HASH_HEADER}" | tr -d '"')
actual=$(sha256sum "${EMBEDDED_LIB}" | awk '{print $1}')

if [ "${expected}" = "${actual}" ]; then
    echo "PASS: embedded lib hash matches header (${actual:0:16}...)"
    exit 0
fi

echo "FAIL: libdd_profiling-embedded.so hash does not match header"
echo "  header:   ${expected}"
echo "  on disk:  ${actual}"
exit 1
