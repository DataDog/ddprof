#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

target=$1
for a in $(objdump -d ${target} | awk '/>:/ {print $1}'); do addr2line -f -e ${target} 0x$a; done
