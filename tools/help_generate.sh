#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

# Set directory names
BASEDIR=$(dirname "$0")
cd $BASEDIR
cd ../
TOP_LVL_DIR=`pwd`

FILE=${TOP_LVL_DIR}/docs/Commands.md
echo "# ddprof Commands" > ${FILE}
echo "" >> ${FILE}
echo '```bash' >> ${FILE}
release/ddprof >> ${FILE}
echo '```' >> ${FILE}
