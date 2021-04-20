#!/bin/bash
docker build -t ddprof/build .
echo binding $(git rev-parse --show-toplevel)
docker run --mount type=bind,source=$(git rev-parse --show-toplevel)/,target=/app -it ddprof/build bash
