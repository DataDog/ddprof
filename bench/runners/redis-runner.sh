#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

redis-server demos/redis.conf > /dev/null 2>&1 &
sleep 1
redis-benchmark -q -l > /dev/null 2>&1
