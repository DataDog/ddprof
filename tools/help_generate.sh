#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

BUILD_FOLDER="build_Release"
# Parse parameters 
if [ $# != 0 ] && [ $1 == "-b" ]; then
    shift
    cd $1
    BUILD_FOLDER="$PWD"
    cd $CURRENTDIR
    shift
    PARAM_FOUND=1
    continue
fi


FILE=${TOP_LVL_DIR}/docs/Commands.md
echo "# ddprof Commands" > ${FILE}
echo "" >> ${FILE}
echo '```bash' >> ${FILE}
${BUILD_FOLDER}/ddprof >> ${FILE}
echo '```' >> ${FILE}
