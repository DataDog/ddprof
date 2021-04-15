#!/bin/bash
REMOTE_DIR=${HOME}/dev/dd-trace-php
LOCAL_DIR=${HOME}/dev/native

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")

for f in $(cat ${SCRIPT_DIR}/mapping.txt); do
  fr=${REMOTE_DIR}/$f
  fl=${LOCAL_DIR}/$f
  if ! cmp -s $fr $fl; then
    if ! vimdiff $fr $fl; then
      exit
    fi
  fi
done
