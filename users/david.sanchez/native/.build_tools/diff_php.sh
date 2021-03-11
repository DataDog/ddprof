#!/bin/bash
REMOTE_DIR=${HOME}/dev/dd-trace-php
LOCAL_DIR=${HOME}/dev/native

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")

for f in $(cat ${SCRIPT_DIR}/mapping.txt); do
  fr=${REMOTE_DIR}/$f
  fl=${LOCAL_DIR}/$f1
  if cmp -s $fr $fl; then
    vimdiff $fr $fl
  fi
done
