#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# This script will install a recent gcc version on Ubuntu

set -euxo pipefail

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root!"
   exit 1
fi

# read optional command line argument
GCC_VERSION=11
if [ "$#" -eq 1 ]; then
    GCC_VERSION=$1
fi

 add-apt-repository -y ppa:ubuntu-toolchain-r/test
 apt-get update
 apt-get install -y "gcc-$GCC_VERSION" "g++-$GCC_VERSION"
