#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

docker build -t ddprof/build .
echo binding $(git rev-parse --show-toplevel)
docker run --mount type=bind,source=$(git rev-parse --show-toplevel)/,target=/app -it ddprof/build bash
