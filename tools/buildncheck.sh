# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

#!/bin/bash
set -euo pipefail
IFS=$'\n\t'

DIR=$(git rev-parse --show-toplevel)
cd ${DIR}
rm -rf release/ddprof
scan-build-11 make build
