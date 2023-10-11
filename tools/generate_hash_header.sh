#!/bin/bash

set -euo pipefail
IFS=$'\n\t'


usage() {
    echo "Usage :"
    echo "$0 <binary_file> <var_name> <header_file_to_generate>"
    echo ""
    echo "Generate an include file with sha256 of input binary file"
}

if [ $# != 3 ] || [ "$1" == "-h" ]; then
    usage
    exit 1
fi


BINARY_FILE=$1
VAR_NAME=$2
HEADER_FILE=$3

SHA256=$(sha256sum "${BINARY_FILE}" | awk '{print $1}')

cat << EOF > "${HEADER_FILE}"
#pragma once
static const char* ${VAR_NAME} = "${SHA256}";
EOF
