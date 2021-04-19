#!/bin/bash
redis-server test/redis.conf &
fio --filename=fio --size=1GB --rw=randrw --bs=4k --ioengine=libaio --iodepth=256 --runtime=120 --numjobs=4 --time_based --group_reporting --name=iops-test-job --eta-newline=1 &
sleep 1
redis-benchmark -q -l
