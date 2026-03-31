#!/usr/bin/env bash

set -euo pipefail
timeout 5s ./test/pthread_deadlock
