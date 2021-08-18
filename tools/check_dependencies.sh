#!/bin/bash

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

# Poor man's test, I'll come back to it
NB_DEPS=$(ldd $1 | wc -l)
NB_EXPECTED_DEPS=$(cat $2 | wc -l)

if [ $NB_DEPS != $NB_EXPECTED_DEPS ]; then
    echo "Check dependencies different number."
    echo "Nb deps = $NB_DEPS vs Expected : $NB_EXPECTED_DEPS"
    ldd $1
    exit 1
fi

exit 0
