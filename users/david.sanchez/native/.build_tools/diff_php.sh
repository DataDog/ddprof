#!/bin/bash
REMOTE_DIR=${HOME}/dev/dd-trace-php
LOCAL_DIR=${HOME}/dev/native

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")

for f in $(cat ${SCRIPT_DIR}/mapping.txt); do
  vmdiff -c wqa! ${REMOTE_DIR}/${f} ${LOCAL_DIR}/${f}
done
