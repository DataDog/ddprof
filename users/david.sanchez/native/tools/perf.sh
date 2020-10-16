#!/bin/bash
ltrace -o /tmp/perf.ltrace -s 25000 -n 2 -x '*' /usr/lib/linux-tools-5.4.0-51/perf $@ 
