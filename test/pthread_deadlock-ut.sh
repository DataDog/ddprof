#!/usr/bin/env bash

set -euo pipefail
timeout 5s ./ddprof -X no -e sALLOC,1 ./test/pthread_deadlock