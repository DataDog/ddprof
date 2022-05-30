#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

usage() {
    echo "Usage :"
    echo "$0 <version> <sha256> <path>"
    echo ""
    echo "Example"
    echo "  $0 645a3ebd 66b5d20c03ea8ea9579b86b243efecf28046660bbe2cfe37db705dcfc53f9d79 ./vendor"
}

if [ $# != 3 ] || [ "$1" == "-h" ]; then
    usage
    exit 1
fi

MARCH=$(uname -m)

TAG_LIBDDPROF=$1
SHA256_LIBDDPROF=$2
TARGET_EXTRACT=$3

# https://github.com/DataDog/libdatadog/releases/download/v0.7.0-rc.1/libdatadog-aarch64-alpine-linux-musl.tar.gz
TAR_LIBDDPROF=libdatadog-${MARCH}-unknown-linux-gnu.tar.gz
GITHUB_URL_LIBDDPROF=https://github.com/DataDog/libdatadog/releases/download/${TAG_LIBDDPROF}/${TAR_LIBDDPROF}

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
if ! echo "${SHA256_LIBDDPROF} ${TAR_LIBDDPROF}" | sha256sum --check --strict --status; then
    echo "Error validating libddprof SHA256"
    echo "Please clear $TARGET_EXTRACT before restarting"
    exit 1
fi

if [ $already_present -eq 0 ]; then
    echo "Extracting libddprof"
    tar xf "${TAR_LIBDDPROF}" --strip-components=1 --no-same-owner
fi
