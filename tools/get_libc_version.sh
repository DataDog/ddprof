#!/bin/bash

if [ -z "${1=""}" ]; then
  echo "specify the type of libc"
  exit 1
fi

# slightly hacky way to check the libc version
if [ ${1} == "musl" ]; then
  ldd --version 2>&1 | grep Version | awk '{print $NF}'
elif [ ${1} == "gnu" ]; then
  ldd --version | head -n 1 | awk '{print $NF}'
else
  echo "LIBC NOT HANDLED"
  exit 1
fi
