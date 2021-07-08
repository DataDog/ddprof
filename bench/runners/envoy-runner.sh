#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/

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
envoy --config-path $TOP_LVL_DIR/demos/envoy-demo.yaml

