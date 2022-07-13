#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

usage() {
    echo "Usage :"
    echo "$0 <version> <path>"
    echo ""
    echo "Example"
    echo "  $0 v0.7.0-rc.1 ./vendor"
}

if [ $# != 2 ] || [ "$1" == "-h" ]; then
    usage
    exit 1
fi

SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname "$SCRIPTPATH")

MARCH=$(uname -m)

TAG_LIBDDPROF=$1
TARGET_EXTRACT=$2

CHECKSUM_FILE=${SCRIPTDIR}/libddprof_checksums.txt

# Test for musl
MUSL_LIBC=$(ldd /bin/ls | grep 'musl' | head -1 | cut -d ' ' -f1 || true)
if [ ! -z ${MUSL_LIBC-""} ]; then
    DISTRIBUTION="alpine-linux-musl"
else
    DISTRIBUTION="unknown-linux-gnu"
fi

# https://github.com/DataDog/libdatadog/releases/download/v0.7.0-rc.1/libdatadog-aarch64-alpine-linux-musl.tar.gz
TAR_LIBDDPROF=libdatadog-${MARCH}-${DISTRIBUTION}.tar.gz
GITHUB_URL_LIBDDPROF=https://github.com/DataDog/libdatadog/releases/download/${TAG_LIBDDPROF}/${TAR_LIBDDPROF}

SHA256_LIBDDPROF=$(grep "${TAR_LIBDDPROF}" ${CHECKSUM_FILE})

mkdir -p "$TARGET_EXTRACT"
cd "$TARGET_EXTRACT"

already_present=0
if [ -e "${TAR_LIBDDPROF}" ]; then
    already_present=1
else
    echo "Downloading libddprof ${TAG_LIBDDPROF}..."
    curl -LO "${GITHUB_URL_LIBDDPROF}"
fi

 echo "Checking libddprof sha256"
 if ! echo "${SHA256_LIBDDPROF}" | sha256sum -c; then
     echo "Error validating libddprof SHA256"
     echo "Please clear $TARGET_EXTRACT before restarting"
     exit 1
 fi

if [ $already_present -eq 0 ]; then
    echo "Extracting libddprof"
    tar xf "${TAR_LIBDDPROF}" --strip-components=1 --no-same-owner
fi
