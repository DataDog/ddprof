#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

set -euo pipefail
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
IFS=$'\n\t'

usage() {
    echo "Usage :"
    echo "$0 <version> <sha1> <path>"
    echo ""
    echo "Example"
    echo "  $0 3.21.0 817d769743d278b5d07526e85115082054e9bf9c ./vendor"
}

if [ "$#" -ne 3 ]; then
    usage
    exit 1
fi

VER_VALGRIND=$1
SHA1_VALGRIND=$2
TARGET_EXTRACT=$3

mkdir -p "${TARGET_EXTRACT}"
cd "${TARGET_EXTRACT}"

TAR_VALGRIND="valgrind-${VER_VALGRIND}.tar.bz2"
URL_VALGRIND="https://sourceware.org/pub/valgrind/${TAR_VALGRIND}"
THIRD_PARTY_PATH="/tmp/deps/${TAR_VALGRIND}"

already_present=0
if [ -e "${THIRD_PARTY_PATH}" ]; then
    echo "Found ${TAR_VALGRIND} in third_party, copying locally"
    cp "${THIRD_PARTY_PATH}" "${TAR_VALGRIND}"
    already_present=1
elif [ -e "${TAR_VALGRIND}" ]; then
    echo "Found ${TAR_VALGRIND} already present locally"
    already_present=1
else
    echo "Downloading ${TAR_VALGRIND} from ${URL_VALGRIND}"
    curl -fsSLO "${URL_VALGRIND}"
fi

echo "Checking valgrind sha1"
if ! echo "${SHA1_VALGRIND}  ${TAR_VALGRIND}" | sha1sum -c; then
    echo "Error validating valgrind SHA1"
    if [ "${already_present}" -eq 0 ]; then
        echo "Removing corrupted download"
        rm "${TAR_VALGRIND}"
    fi
    exit 1
fi

echo "Extracting ${TAR_VALGRIND}"
tar xf "${TAR_VALGRIND}"

echo "Building and installing valgrind"
pushd "valgrind-${VER_VALGRIND}"
./configure --prefix /usr/local
make -j $(nproc)
make install
valgrind --version
popd

echo "Cleaning up"
rm -rf "valgrind-${VER_VALGRIND}" "${TAR_VALGRIND}"

echo "Valgrind installation completed successfully" 