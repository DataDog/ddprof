#!/usr/bin/env bash

# Test the loader's exe-path discovery when using an installed
# libdd_profiling-embedded.so (USE_LOADER=ON only).
#
# The standalone embedded lib does not embed the ddprof binary.  The loader
# must find the ddprof executable relative to its own path using two layouts:
#
#   1. Flat build/dev layout:  <loader_dir>/ddprof
#   2. Install layout:         <loader_dir>/../bin/ddprof
#
# Both are exercised here without requiring a full profiling run: we verify
# that the loader sets DD_PROFILING_NATIVE_DDPROF_EXE to a valid executable
# path.  We use LD_PRELOAD with a full path so that dladdr() inside the loader
# resolves to the test directory, making the relative path search testable.

set -euo pipefail

BUILDDIR="${PWD}"
EMBEDDED_LIB="${BUILDDIR}/libdd_profiling-embedded.so"
SHARED_LIB="${BUILDDIR}/libdd_profiling.so"

# Only meaningful for loader builds.  In loader builds the shared lib contains
# the string "libdd_profiling-embedded.so" (the bare name used for dlopen).
if ! strings "${SHARED_LIB}" | grep -q "libdd_profiling-embedded.so" 2>/dev/null; then
    echo "INFO: non-loader build — skipping loader exe-lookup test"
    echo "PASS: all checks passed"
    exit 0
fi

echo "INFO: loader build — testing exe path discovery"

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT

# Use a tiny stub executable as the fake ddprof — any executable satisfies
# access(path, X_OK). This avoids copying the real 71 MB ddprof binary.
FAKE_DDPROF="${tmpdir}/fake_ddprof"
printf '#!/bin/sh\nexit 0\n' > "${FAKE_DDPROF}"
chmod +x "${FAKE_DDPROF}"

# ---------------------------------------------------------------------------
# Helper: inject the loader from LOADER_DIR via LD_PRELOAD, put the embedded
# lib on LD_LIBRARY_PATH (same dir or lib/ subdir), then exec `env` so we
# can read the environment the loader set up.
# Returns the value of DD_PROFILING_NATIVE_DDPROF_EXE.
# ---------------------------------------------------------------------------
get_resolved_exe() {
    local loader_path="$1"   # full path to libdd_profiling.so to inject
    local lib_search="$2"    # dir for LD_LIBRARY_PATH (embedded lib location)

    LD_PRELOAD="${loader_path}" \
    LD_LIBRARY_PATH="${lib_search}" \
        "${BUILDDIR}/test/simple_malloc-static" \
        --exec env 2>/dev/null \
        | grep "^DD_PROFILING_NATIVE_DDPROF_EXE=" \
        | sed 's/^DD_PROFILING_NATIVE_DDPROF_EXE=//' \
        || true
}

# ---------------------------------------------------------------------------
# Test 1 — flat layout: libdd_profiling.so, libdd_profiling-embedded.so, and
#           ddprof all live in the same directory.
# ---------------------------------------------------------------------------
echo "--- Test 1: flat layout ---"
flat_dir="${tmpdir}/flat"
mkdir -p "${flat_dir}"
cp "${EMBEDDED_LIB}"  "${flat_dir}/libdd_profiling-embedded.so"
cp "${SHARED_LIB}"    "${flat_dir}/libdd_profiling.so"
cp "${FAKE_DDPROF}"   "${flat_dir}/ddprof"

resolved="$(get_resolved_exe "${flat_dir}/libdd_profiling.so" "${flat_dir}")"
if [[ "${resolved}" == "${flat_dir}/ddprof" ]]; then
    echo "PASS: flat layout — resolved to ${resolved}"
else
    echo "FAIL: flat layout — expected ${flat_dir}/ddprof, got '${resolved}'"
    exit 1
fi

# ---------------------------------------------------------------------------
# Test 2 — install layout: loader and embedded lib in lib/, ddprof in bin/.
# ---------------------------------------------------------------------------
echo "--- Test 2: install layout ---"
install_dir="${tmpdir}/install"
mkdir -p "${install_dir}/lib" "${install_dir}/bin"
cp "${EMBEDDED_LIB}"  "${install_dir}/lib/libdd_profiling-embedded.so"
cp "${SHARED_LIB}"    "${install_dir}/lib/libdd_profiling.so"
cp "${FAKE_DDPROF}"   "${install_dir}/bin/ddprof"

resolved="$(get_resolved_exe "${install_dir}/lib/libdd_profiling.so" \
                              "${install_dir}/lib")"
resolved_real="$(readlink -f "${resolved}" 2>/dev/null || echo "${resolved}")"
expected_real="$(readlink -f "${install_dir}/bin/ddprof")"
if [[ "${resolved_real}" == "${expected_real}" ]]; then
    echo "PASS: install layout — resolved to ${resolved}"
else
    echo "FAIL: install layout — expected ${expected_real}, got '${resolved_real}'"
    exit 1
fi

echo "PASS: all checks passed"
