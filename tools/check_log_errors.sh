#!/bin/bash

if [ ! -e $1 ];then 
    echo "Error file does not exist"
    exit 1
fi

### TRACES TO FIND IN EXECUTION ###
# fragile but simple pattern : check for traces to make sure we followed expected flow
declare -a arr_expected=("Entering main loop" "unwind.ticks")

for trace in "${arr_expected[@]}"
do
    expected_trace=$(grep "${trace}" ${1})
    if [ -z "${expected_trace-=''}" ]; then
        echo "error : unable to find pattern ${trace}"
        echo "---------------- Dump trace for analysis ----------------"
        cat $1
        exit 1
    fi
done

### TRACES TO AVOID IN EXECUTION ###
declare -a arr_bad_traces=("<error>" "Sanitizer")
for trace in "${arr_bad_traces[@]}"
do
    bad_trace=$(grep -i ${trace} ${1})
    #echo "$bad_trace"
    if [ ! -z "${bad_trace-=''}" ]; then
        echo "error : found bad pattern in logs ${trace}"
        echo "---------------- Dump trace for analysis ----------------"
        cat $1
        exit 1
    fi
done

exit 0
