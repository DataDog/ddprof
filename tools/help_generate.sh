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

FILE=${TOP_LVL_DIR}/docs/Commands.md
echo "# ddprof Commands" > ${FILE}
echo "" >> ${FILE}
echo '```bash' >> ${FILE}
release/ddprof >> ${FILE}
echo '```' >> ${FILE}
