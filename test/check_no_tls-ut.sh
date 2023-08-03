#!/usr/bin/env bash

set -euo pipefail

LD_PRELOAD=./test/no_tls/libno_tls.so ./ddprof sleep 1
