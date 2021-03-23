#!/bin/bash
target=$1
for a in $(objdump -d ${target} | awk '/>:/ {print $1}'); do addr2line -f -e ${target} 0x$a; done
