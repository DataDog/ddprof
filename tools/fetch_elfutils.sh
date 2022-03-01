#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

set -euo pipefail
IFS=$'\n\t'

usage() {
    echo "Usage :"
    echo "$0 <version> <md5> <path> <c-compiler>"
    echo ""
    echo "Example"
    echo "  $0 0.183 6f58aa1b9af1a5681b1cbf63e0da2d67 ./vendor gcc"
}

if [ "$#" -ne 4 ]; then
    usage
    exit 1
fi

VER_ELF=$1
SHA256_ELF=$2
mkdir -p ${3}
cd ${3}
DOWNLOAD_PATH=$PWD
TARGET_EXTRACT=${DOWNLOAD_PATH}/elfutils
C_COMPILER=${4}


TAR_ELF="elfutils-${VER_ELF}.tar.bz2"
URL_ELF="https://sourceware.org/elfutils/ftp/${VER_ELF}/${TAR_ELF}"

if [ -e "${TARGET_EXTRACT}" ]; then
    rm -rf ${TARGET_EXTRACT}/*
fi

mkdir -p ${DOWNLOAD_PATH}
cd ${DOWNLOAD_PATH}
if [ ! -e ${DOWNLOAD_PATH}/${TAR_ELF} ]; then
    curl -L --remote-name-all ${URL_ELF}
fi

echo "Checking md5 of elfutils..."
echo "${SHA256_ELF} ${TAR_ELF}" | sha256sum --check --strict --status

echo "Extract elfutils..."
mkdir -p ${TARGET_EXTRACT}
tar --no-same-owner -C ${TARGET_EXTRACT} --strip-components 1 -xf ${DOWNLOAD_PATH}/${TAR_ELF}
rm -f ${DOWNLOAD_PATH}/${TAR_ELF}

echo "Compile elfutils using ${C_COMPILER}"

# The flags below are hardcoded to work around clang compatibility issues in
# elfutils 186; these are irrelevant for GCC.  Note that this won't propagate
# envvars back up to the calling build system.
# TODO is there a better way to infer compiler family?
if [[ "$(basename "${C_COMPILER}")" == clang* ]]; then
  export CFLAGS="-Wno-xor-used-as-pow -Wno-gnu-variable-sized-type-not-at-end -Wno-unused-but-set-parameter"
fi
cd ${TARGET_EXTRACT} && ./configure CC=${C_COMPILER} --without-bzlib --without-zstd --disable-debuginfod --disable-libdebuginfod --disable-symbol-versioning
make -j4 -C ${TARGET_EXTRACT}
