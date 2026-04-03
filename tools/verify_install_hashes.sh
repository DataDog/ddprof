#!/bin/bash

# Verify that the installed libdd_profiling-embedded.so has the same SHA-256
# as the hash baked into the loader.  Run this after cmake --install and
# before packaging to catch hash mismatches early.
#
# Usage: verify_install_hashes.sh <build_dir> <install_prefix>
#   build_dir      — cmake build directory (contains the hash header)
#   install_prefix — cmake install prefix  (contains lib/ and bin/)

set -euo pipefail

if [ $# -ne 2 ]; then
    echo "Usage: $0 <build_dir> <install_prefix>" >&2
    exit 1
fi

BUILD_DIR="$1"
INSTALL_PREFIX="$2"
HASH_HEADER="${BUILD_DIR}/libdd_profiling-embedded_hash.h"
INSTALLED_LIB="${INSTALL_PREFIX}/lib/libdd_profiling-embedded.so"

if [ ! -f "${HASH_HEADER}" ]; then
    echo "SKIP: ${HASH_HEADER} not found (USE_LOADER=OFF?)" >&2
    exit 0
fi

if [ ! -f "${INSTALLED_LIB}" ]; then
    echo "SKIP: ${INSTALLED_LIB} not found" >&2
    exit 0
fi

expected=$(grep -oP '"[0-9a-f]{64}"' "${HASH_HEADER}" | tr -d '"')
actual=$(sha256sum "${INSTALLED_LIB}" | awk '{print $1}')

if [ "${expected}" = "${actual}" ]; then
    echo "PASS: installed libdd_profiling-embedded.so hash matches (${actual:0:16}...)"
    exit 0
fi

echo "FAIL: hash mismatch for installed libdd_profiling-embedded.so" >&2
echo "  expected: ${expected}" >&2
echo "  actual:   ${actual}" >&2
echo "" >&2
echo "The installed .so was likely modified after the hash was computed." >&2
echo "Common cause: cmake --install --strip re-strips the binary." >&2
exit 1
