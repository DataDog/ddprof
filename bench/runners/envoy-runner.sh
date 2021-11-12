#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

set -euo pipefail
IFS=$'\n\t'

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

# Tell the user what to run
echo "while true;do curl -s -o /dev/null http://localhost:10000; done"

# Presumes envoy is installed
envoy --config-path $TOP_LVL_DIR/demos/envoy-demo.yaml >/dev/null

while true; do curl -s -o /dev/null http://localhost:10000; done &
while true; do curl -s -o /dev/null http://localhost:10000; done &
while true; do curl -s -o /dev/null http://localhost:10000; done &

