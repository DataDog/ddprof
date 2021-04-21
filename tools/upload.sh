#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

###
RELNAME=$(release/ddprof --version | sed 's/ /_/g')
aws s3 cp --region us-east-1 --sse AES256 "release/ddprof" "s3://binaries.ddbuild.io/ddprof/release/"${RELNAME}
aws s3 cp --region us-east-1 --sse AES256 "release/ddprof" "s3://binaries.ddbuild.io/ddprof/release/ddprof"
