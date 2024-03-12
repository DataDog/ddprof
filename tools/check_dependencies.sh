#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

set -eu

usage() {
    echo "$0 <executable> <expected_dependency_file>"
    echo ""
    echo "Example :"
    echo "$0 ./deliverables/Release/ddprof ./test/data/expected_deps.txt"
}

if [ $# != 2 ] || [ $1 == "-h" ]; then
    usage
    exit 0
fi

actual_deps=$(readelf -d "$1" | grep '(NEEDED) *Shared library: \[' | awk '{ print $5 }' | tr -d '[]' | sort -u)
expected_deps=$(sort -u < "$2")

unexpected_deps=$(comm -13 <(echo "$expected_deps") <(echo "$actual_deps"))

if [ -n "$unexpected_deps" ]; then
    echo "Unexpected dependencies:" 1>&2
    echo $unexpected_deps 1>&2
    exit 1
fi

exit 0
