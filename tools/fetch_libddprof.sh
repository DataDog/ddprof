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

if [ $# != 3 ] || [ $1 == "-h" ]; then
    usage
    exit 1
fi

TAG_LIBDDPROF=$1
TAR_LIBDDPROF=libddprof_${TAG_LIBDDPROF}.tar.gz
GITHUB_URL_LIBDDPROF=https://github.com/DataDog/libddprof/releases/download/${TAG_LIBDDPROF}/libddprof-x86_64-unknown-linux-gnu.tar.gz

SHA256_LIBDDPROF=$2
mkdir -p $3
cd $3
DOWNLOAD_PATH=$PWD
TARGET_EXTRACT=${DOWNLOAD_PATH}/libddprof

if [ -e "${TARGET_EXTRACT}" ]; then
    rm -rf ${TARGET_EXTRACT}/*
fi

mkdir -p ${TARGET_EXTRACT}

if [ ! -e  ${TAR_LIBDDPROF} ]; then
    echo "Downloading libddprof..."
    curl -L ${GITHUB_URL_LIBDDPROF} -o ${TAR_LIBDDPROF}
fi
SHA_TAR=$(sha256sum ${DOWNLOAD_PATH}/${TAR_LIBDDPROF} | cut -d' ' -f1)

if [ $SHA_TAR != ${SHA256_LIBDDPROF} ];then
    echo "Error validating libddprof"
    echo "Got following SHA: ${SHA_TAR} (instead of ${SHA256_LIBDDPROF})"
    echo "Please clear ${DOWNLOAD_PATH}/${TAR_LIBDDPROF} before restarting"
    exit 1
fi

tar xvfz ${DOWNLOAD_PATH}/${TAR_LIBDDPROF} -C ${TARGET_EXTRACT}
