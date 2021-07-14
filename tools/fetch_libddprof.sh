#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

usage() {
    echo "Usage :"
    echo "$0 <version> <sha256> <path>"
    echo ""
    echo "Example"
    echo "  $0 645a3ebd 5384319ab8dec373ad06e24c288522b3af464ddfd1dbd4d2ba03870312b635ba ./vendor"
}

if [ $# != 3 ] || [ $1 == "-h" ]; then
    usage
    exit 0
fi

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

VER_LIBDDPROF=$1
TAR_LIBDDPROF=libddprof_${VER_LIBDDPROF}.tar.gz
URL_LIBDDPROF=http://binaries.ddbuild.io/libddprof/${TAR_LIBDDPROF}
S3_URL_LIBDDPROF=s3://binaries.ddbuild.io/libddprof/${TAR_LIBDDPROF}

SHA256_LIBDDPROF=$2
mkdir -p $3
cd $3
DOWNLOAD_PATH=$PWD
TARGET_EXTRACT=${DOWNLOAD_PATH}/libddprof

if [ -e ${TARGET_EXTRACT} ]; then
    echo "Error, clean the directory : ${TARGET_EXTRACT}"
    exit 1
fi

IS_CI=${CI:-false}
if [ ! -e  ${TAR_LIBDDPROF} ]; then
    if [ ${IS_CI} == "false" ]; then
        # Http works locally
        echo "Download using curl..."
        curl -L ${URL_LIBDDPROF} -o ${TAR_LIBDDPROF}
    else # CI flow uses aws cli
        #Locally you can use aws vault to test this command
        #aws-vault exec build-stable-developer -- aws s3 cp ${S3_URL_LIBDDPROF} ${DOWNLOAD_PATH}
        echo "Download using s3..."
        aws s3 cp ${S3_URL_LIBDDPROF} ${DOWNLOAD_PATH}
    fi
fi
SHA_TAR=$(sha256sum ${DOWNLOAD_PATH}/${TAR_LIBDDPROF} | cut -d' ' -f1)

if [ $SHA_TAR != ${SHA256_LIBDDPROF} ];then
    echo "Error validating libddprof"
    echo "Please clear ${DOWNLOAD_PATH}/${TAR_LIBDDPROF} before restarting"
    exit 1
fi

# Rust target not used for now, only extract deliverables
tmp_dir=$(mktemp -d -t deliverables-XXXXXXXXXX)
echo "Extract to $tmp_dir"
cd $tmp_dir
tar -xvf ${DOWNLOAD_PATH}/${TAR_LIBDDPROF} deliverables
mv deliverables ${TARGET_EXTRACT}
rmdir $tmp_dir
cd -
exit 0
