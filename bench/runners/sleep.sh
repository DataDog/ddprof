#!/bin/bash

NB_ITERATIONS=10000000
if [ ! -z $1 ]; then
    NB_ITERATIONS=$1
    # convert back to seconds
    NB_ITERATIONS=$(($NB_ITERATIONS * 100))
fi

for ((i = 0; i < ${NB_ITERATIONS}; ++i)); do
    sleep 0.01
done

echo "nbComputations=${NB_ITERATIONS}"
