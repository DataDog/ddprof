#!/usr/bin/env bash

set -euo pipefail

export DD_PROFILING_NATIVE_USE_EMBEDDED_LIB=1
export LD_LIBRARY_PATH=$PWD
export DD_PROFILING_NATIVE_CONFIG="${PWD}/../test/configs/simple_malloc_config.toml"

use_taskset=1
if [[ "${DD_PROFILING_REORDER_EVENTS:-0}" -eq 1 ]]; then
    # when reordering events, no workarounds are needed
    use_taskset=0
fi

opts="--loop 1000 --spin 100"

if [[ "${use_taskset}" -eq 1 ]]; then
    # Get available cpus
    # ddprof will be allowed to run on those cpus
    # This is necessary since we set a single cpu for the test and ddprof will inherit the taskset
    # (for wrapper mode we could do `ddprof ... taskset <cpu_mask> <my_test>` but this is not possible
    # for library mode).
    ddprof_cpu_mask=$(python3 -c 'import os;print(hex(sum(1 << c for c in os.sched_getaffinity(0))))')
    export DD_PROFILING_NATIVE_CPU_AFFINITY=${ddprof_cpu_mask}

    # select a random cpu to run test on
    # the goal is to run test on only one cpu in order to avoid event reordering
    # (seeing cpu event before mmap events because of a cpu migration)
    test_cpu_mask=$(python3 -c 'import random,os;print(hex(1 << random.choice(list(os.sched_getaffinity(0)))))')

    # Setting a nice value for simple malloc will help ddprof get scheduled
    opts="$opts --nice 19"
fi

timeout_sec=30
log_file=$(mktemp "${PWD}/log.XXXXXX")
echo "Logs available in $log_file"
rm "${log_file}"
export DD_PROFILING_NATIVE_LOG_MODE="${log_file}"

count() {
    log_file="$1"
    sample_type="$2"
    pid_or_tid="$3"
    local result
    set +e
    set +o pipefail
    result=$(grep "sample\[type=${sample_type};.*;do_lot_of_allocations" "${log_file}" | grep -o "${pid_or_tid}=[0-9]*" | sort -u | wc -l)
    local exit_status=$?
    # Re-enable exit on error and pipefail
    set -e
    set -o pipefail
    if [ $exit_status -ne 0 ]; then
        echo "Error occurred while counting for sample_type=${sample_type} and pid_or_tid=${pid_or_tid}" >&2
        exit $exit_status
    fi
    echo "$result"
}

check_logs() {
    cmd="$1"
    expected_pids="$2"
    expected_tids="${3-$2}"
    # Create a list of profile types with comma as separator
    IFS=',' read -ra list_of_profile_types_to_count <<< "${4-"alloc-space,cpu-time"}"
    if [[ "${expected_pids}" -ne -1 ]]; then
        for profile_type in "${list_of_profile_types_to_count[@]}"; do
            counted_pids=$(count "${log_file}" "${profile_type}" "pid")
            counted_tids=$(count "${log_file}" "${profile_type}" "tid")
            if [[ $counted_pids -ne "${expected_pids}"||
                $counted_tids -ne "${expected_tids}" ]]; then
              echo "Incorrect number of sample found for: $cmd"
              echo "profile-type:${profile_type}"
              echo "counted_pids = ${counted_pids}"
              echo "counted_tids = ${counted_tids}"
              cat "${log_file}"
              exit 1
            fi
        done
    else
        if [ -s "${log_file}" ]; then
            echo "Unexpected samples for $cmd"
            cat "${log_file}"
            exit 1
        fi
    fi
    rm -f "${log_file}"
}

check() {
    cmd="$1"
    expected_pids="$2"
    expected_tids="${3-$2}"
    if [[ "$use_taskset" -eq 1 ]]; then
        cmd="taskset ${test_cpu_mask} ${cmd}"
    fi    
    echo "Running: ${cmd}"
    # shellcheck disable=SC2086
    eval ${cmd} || ( echo "Command failed: ${cmd}" && cat "${log_file}" && exit 1 )
    if [[ "${expected_pids}" -ne -1 ]]; then
        sync "${log_file}"
        # -P requires GNU grep
        ddprof_pid=$(grep -m1 -oP ' ddprof\[\K[0-9]+(?=\]: Starting profiler)' "${log_file}" || true)
        if [ -z "${ddprof_pid}" ]; then
            echo "Unable to find profiler pid"
            cat "${log_file}"
            exit 1
        fi
        # --pid requires GNU tail
        timeout "$timeout_sec" tail --pid="$ddprof_pid" -f /dev/null
        sync "${log_file}"
    fi
    check_logs "$@"
}

# Test disabled static lib mode
check "./test/simple_malloc-static ${opts}" -1

# Test enabled static lib mode
check "./test/simple_malloc-static --profile ${opts}" 1

# Test disabled shared lib mode
check "./test/simple_malloc-shared ${opts}" -1

# Test enabled shared lib mode
check "./test/simple_malloc-shared --profile ${opts}" 1

# Test wrapper mode
check "./ddprof ./test/simple_malloc ${opts}" 1

# Test live heap mode, CPU events are given through configuration file
event="sALLOC,period=-524288,mode=l;sCPU"
check "./ddprof --show_config --event \"${event}\" ./test/simple_malloc ${opts} --skip-free 100" 1 1 "inuse-space,cpu-time"

# Test live heap mode, with allocations, CPU events are given through configuration file
event="sALLOC,period=-524288,mode=sl;sCPU"
check "./ddprof --show_config --event \"${event}\" ./test/simple_malloc ${opts} --skip-free 100" 1 1 "inuse-space,alloc-space,cpu-time"

# Test wrapper mode with forks + threads
opts_more_spin="--loop 1000 --spin 400"
check "./ddprof ./test/simple_malloc ${opts_more_spin} --fork 2 --threads 2" 2 4

# Test dlopen
check "./ddprof ./test/simple_malloc --use-shared-library ${opts}" 1

# Test loaded libs check (with dlopen that avoids hook)
check "./ddprof --initial-loaded-libs-check-delay 500 ./test/simple_malloc --use-shared-library --avoid-dlopen-hook --initial-delay 1000 ${opts}" 1

# Test loaded libs check (fork + dlopen that avoids hook)
check "./ddprof --initial-loaded-libs-check-delay 500 ./test/simple_malloc --use-shared-library --avoid-dlopen-hook --fork 2 --initial-delay 1000 ${opts}" 2

# Test slow profiler startup
check "env DD_PROFILING_NATIVE_STARTUP_WAIT_MS=200 ./ddprof ./test/simple_malloc ${opts}" 1

# Test follow execs
check "./ddprof bash -c \"./test/simple_malloc ${opts}\"" 1

# Test disabling follow execs
check "env DD_PROFILING_NATIVE_ALLOCATION_PROFILING_FOLLOW_EXECS=0 DD_PROFILING_NATIVE_STARTUP_WAIT_MS=200 ./ddprof bash -c \"./test/simple_malloc ${opts}\"" 0 0 "inuse-space"

# Test switching user
if runuser -u ddbuild /usr/bin/true &> /dev/null; then
    check "./ddprof --switch_user ddbuild ./test/simple_malloc ${opts}" 1
fi

# Test pid mode with multiple threads
echo "Running: ./test/simple_malloc ${opts} --threads 2 --stop"
./test/simple_malloc $opts --threads 2 --stop&
pid=$!
# wait for simple_malloc to be stopped
while ! awk '{print $3}' /proc/${pid}/stat | grep -q T; do
    sleep 0.1
done
./ddprof --pid ${pid} -e sCPU &
ddprof_pid=$!
# wait for ddprof to be ready
sleep 0.5
# resume simple_malloc process
kill -SIGCONT ${pid}
# wait for ddprof to finish
wait ${ddprof_pid}
check_logs "./test/simple_malloc ${opts} --threads 2 --stop" 1 2 "cpu-time"
