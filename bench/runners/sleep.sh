# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

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
