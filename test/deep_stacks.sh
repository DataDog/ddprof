#!/usr/bin/env bash

CONFIG_DEEP_STACK="../test/configs/deep_stacks.toml"

./ddprof --config ${CONFIG_DEEP_STACK} ./test/deep_stacks | tee ./log_ddprof.txt

# Check for truncated stack traces
truncated_input=$(awk -F': ' '/datadog.profiling.native.unwind.stack.truncated_input/ { print $NF }' ./log_ddprof.txt)

# If we fail to run ddprof we should also be missing the metric
if [ "$truncated_input" != "0" ]; then
  echo "Found truncated stack traces!"
  echo "Truncated input: $truncated_input"
  exit 1
else
  echo "No truncated stack traces found."
fi
exit 0
