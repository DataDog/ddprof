#!/bin/bash
set -euo pipefail
IFS=$'\n\t'

DIR=$(git rev-parse --show-toplevel)
cd ${DIR}
rm -rf release/ddprof
scan-build-11 make build
