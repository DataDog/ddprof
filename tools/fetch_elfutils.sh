#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

set -euo pipefail
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
IFS=$'\n\t'

usage() {
    echo "Usage :"
    echo "$0 <version> <sha256> <path> <c-compiler> <c-flags-override>"
    echo ""
    echo "The extra c flags should be a single arg (hence quoted)"
    echo "If specified, the default c flags should include \" -g -O2\""
    echo "Example"
    echo "  $0 0.183 6f58aa1b9af1a5681b1cbf63e0da2d67 ./vendor gcc \"-O0 -g\""
}

if [ "$#" -lt 4 ] || [ "$#" -ge 6 ]; then
    usage
    exit 1
fi

VER_ELF=$1
SHA512_ELF=$2
TARGET_EXTRACT=$3
C_COMPILER=${4}
C_FLAGS_OVERRIDE=""
# C flags are optional
if [ "$#" -ge 5 ]; then
  C_FLAGS_OVERRIDE=${5}
fi

mkdir -p "${TARGET_EXTRACT}"
cd "${TARGET_EXTRACT}"

TAR_ELF="elfutils-${VER_ELF}.tar.bz2"
URL_ELF="https://sourceware.org/elfutils/ftp/${VER_ELF}/${TAR_ELF}"
THIRD_PARTY_PATH="/tmp/deps/${TAR_ELF}"

# Check if already built (highest priority)
if [ -d "${TARGET_EXTRACT}/lib" ] && [ -d "${TARGET_EXTRACT}/include" ]; then
    echo "elfutils already built in ${TARGET_EXTRACT}, skipping"
    exit 0
fi

# Check if tar already present locally (second priority)
if [ -e "${TAR_ELF}" ]; then
    echo "Found ${TAR_ELF} already present locally"
# Check if available in cache (third priority - copy but continue building)
elif [ -e "${THIRD_PARTY_PATH}" ]; then
    echo "Found ${TAR_ELF} in deps cache, copying locally"
    cp "${THIRD_PARTY_PATH}" "${TAR_ELF}"
else
    echo "Downloading ${TAR_ELF} from ${URL_ELF}"
    curl -fsSLO "${URL_ELF}"
fi

echo "Checking elfutils sha256"
if ! echo "${SHA512_ELF}  ${TAR_ELF}" | sha256sum -c ; then
    echo "Error validating elfutils SHA256"
    echo "Please clear $TARGET_EXTRACT before restarting"
    exit 1
fi

# Continue with extraction and building
echo "Extracting elfutils"
mkdir src
mkdir lib
cd src
tar --no-same-owner --strip-components 1 -xf "../${TAR_ELF}"

patch -p1 < "${SCRIPT_DIR}/elfutils.patch"

# The flags below are hardcoded to work around clang compatibility issues in
# elfutils 186; these are irrelevant for GCC.  Note that this won't propagate
# envvars back up to the calling build system.
# TODO is there a better way to infer compiler family?
if [[ "$(basename "${C_COMPILER}")" == clang* ]]; then
  export CFLAGS="-Wno-xor-used-as-pow -Wno-gnu-variable-sized-type-not-at-end -Wno-unused-but-set-parameter"
fi

# Detect musl
MUSL_LIBC=$(ldd /bin/ls | grep 'musl' | head -1 | cut -d ' ' -f1 || true)
if [[ -n ${MUSL_LIBC-""} ]]; then
  cp /patch/libintl.h ../lib
  patch -p1 < "/patch/fix-aarch64_fregs.patch"
  patch -p1 < "/patch/fix-uninitialized.patch"
  patch -p1 < "/patch/musl-asm-ptrace-h.patch"
  patch -p1 < "/patch/musl-macros.patch"
  patch -p1 < "/patch/libdw_alloc.c.patch"
else
  echo "LIB is not detected as musl"
fi

# It is important NOT to set CFLAGS if you don't mean to
# Otherwise you will not benefit from -O2 in the elfutils compilation
if [ -n "${C_FLAGS_OVERRIDE-""}" ]; then
  export CFLAGS="${CFLAGS-""} ${C_FLAGS_OVERRIDE}"
else # Include the default flags for elfutils
  if [ -n "${CFLAGS-""}" ]; then
    export CFLAGS="${CFLAGS-""} -g -O2"
  fi
fi

# Fix _FORTIFY_SOURCE redefinition issue with newer elfutils on Alpine/musl
if [[ -n ${MUSL_LIBC-""} ]]; then
  export CFLAGS="${CFLAGS-""} -Wno-error=builtin-macro-redefined"
  export CXXFLAGS="${CXXFLAGS-""} -Wno-error=builtin-macro-redefined"
fi

echo "Compiling elfutils using ${C_COMPILER} / flags=${CFLAGS-""}"

./configure CC="${C_COMPILER}" --with-zlib --with-bzlib --with-lzma --with-zstd --disable-debuginfod --disable-libdebuginfod --disable-symbol-versioning --prefix "${TARGET_EXTRACT}"

make "-j$(nproc)" install
