#!/bin/bash
redis-server test/redis.conf > /dev/null 2>&1 &
fio --filename=fio --size=5MB --rw=randrw --bs=4k --ioengine=libaio --iodepth=256 --runtime=120 --numjobs=4 --time_based --group_reporting --name=iops-test-job --eta-newline=1 &
sleep 1
redis-benchmark -q -l > /dev/null 2>&1
