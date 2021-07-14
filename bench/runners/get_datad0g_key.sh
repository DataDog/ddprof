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

# Read data dog keys
if [ ! -f ${TOP_LVL_DIR}/.env ]; then
  echo "Please fill your datadog keys in the .env file (refer to Build.md)"
  exit 0
fi 
source ${TOP_LVL_DIR}/.env
if [ -z ${DD_API_DATAD0G_KEY} ]; then
  echo "Please fill your staging key DD_API_DATAD0G_KEY=<value> in the .env file (in the root of the project)"
  exit 0
fi

echo ${DD_API_DATAD0G_KEY}
exit 0
