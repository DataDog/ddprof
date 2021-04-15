#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")

###
aws-vault exec build-stable-developer -- \
  aws s3 cp --region us-east-1 --sse AES256 "build/$1" "s3://binaries.ddbuild.io/ddprof/release/$1"
