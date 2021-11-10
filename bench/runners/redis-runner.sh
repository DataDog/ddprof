# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

#!/bin/bash
redis-server demos/redis.conf > /dev/null 2>&1 &
#fio --filename=fio --size=5MB --rw=randrw --bs=4k --ioengine=libaio --iodepth=256 --runtime=120 --numjobs=4 --time_based --group_reporting --name=iops-test-job --eta-newline=1 &
sleep 1
redis-benchmark -q -l > /dev/null 2>&1
