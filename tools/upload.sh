#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

###
PRE="s3://binaries.ddbuild.io/ddprof/release"
REL_FULL=$(release/ddprof --version | sed -e 's/ /_/g' -e 's/\+/_/g')
REL_VER=$(release/ddprof --version | sed -e 's/ /_/g' -e 's/\+.*//g')
aws s3 cp --region us-east-1 --sse AES256 "release/ddprof" "${PRE}/${REL_FULL}"
aws s3 cp --region us-east-1 --sse AES256 "release/ddprof" "${PRE}/${REL_VER}"
aws s3 cp --region us-east-1 --sse AES256 "release/ddprof" "${PRE}/ddprof"
