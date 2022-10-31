#!/usr/bin/env bash

set -euo pipefail

export DD_PROFILING_EXPORT=no
export DD_PROFILING_NATIVE_SHOW_SAMPLES=1
export DD_PROFILING_NATIVE_USE_EMBEDDED_LIB=0
export DD_PROFILING_NATIVE_LOG_LEVEL=debug
export LD_LIBRARY_PATH=$PWD
export DD_PROFILING_NATIVE_PRESET=default
# force deterministic sampling
export DD_PROFILING_NATIVE_EVENTS="sALLOC period=-524288"

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

opts="--loop 1000 --spin 100"
log_file=$(mktemp "${PWD}/log.XXXXXX")
rm "${log_file}"
export DD_PROFILING_NATIVE_LOG_MODE="${log_file}"

count() {
    log_file="$1"
    sample_type="$2"
    pid_or_tid="$3"
    grep "sample\[type=${sample_type};.*;do_lot_of_allocations" "${log_file}" | grep -o "${pid_or_tid}=[0-9]*" | sort -u | wc -l
}

check() {
    cmd="$1"
    expected_pids="$2"
    expected_tids="${3-$2}"
    # shellcheck disable=SC2086
    taskset "${test_cpu_mask}" ${cmd}

    if [[ "${expected_pids}" -eq 1 ]]; then
        # Ugly workaround for tail bug that makes it wait indefinitely for new lines when `grep -q` exists:
        # https://debbugs.gnu.org/cgi/bugreport.cgi?bug=13183
        # https://superuser.com/questions/270529/monitoring-a-file-until-a-string-is-found
        coproc tail -F "${log_file}"
        grep -Fq "Profiling terminated" <&"${COPROC[0]}"
        kill "$COPROC_PID"
    fi
    if [[ "${expected_pids}" -ne 0 ]]; then
        counted_pids_alloc=$(count "${log_file}" "alloc-samples" "pid")
        counted_pids_cpu=$(count "${log_file}" "cpu-samples" "pid")
        counted_tids_alloc=$(count "${log_file}" "alloc-samples" "tid")
        counted_tids_cpu=$(count "${log_file}" "cpu-samples" "tid")
        if [[ $counted_pids_alloc -ne "${expected_pids}" ||
            $counted_pids_cpu -ne "${expected_pids}" ||
            $counted_tids_alloc -ne "${expected_tids}" ||
            $counted_tids_cpu -ne "${expected_tids}" ]]; then
            echo "Incorrect number of sample found for: $cmd"
            echo "counted_pids_alloc = $counted_pids_alloc"
            echo "counted_pids_cpu = ${counted_pids_cpu}"
            echo "counted_tids_alloc = ${counted_tids_alloc}"
            echo "counted_tids_cpu = ${counted_tids_cpu}"
            cat "${log_file}"
            exit 1
        fi
    else
        if [ -f "${log_file}" ]; then
            echo "Unexpected samples for $cmd"
            cat "${log_file}"
            exit 1
        fi
    fi
    rm -f "${log_file}"
}

# Test disabled static lib mode
check "./test/simple_malloc-static ${opts}" 0

# Test enabled static lib mode
#check "./test/simple_malloc-static --profile ${opts}" 1

# Test disabled shared lib mode
check "./test/simple_malloc-shared ${opts}" 0

# Test enabled shared lib mode
check "./test/simple_malloc-shared --profile ${opts}" 1

# Test wrapper mode
check "./ddprof ./test/simple_malloc ${opts}" 1

# Test wrapper mode with forks + threads
check "./ddprof ./test/simple_malloc ${opts} --fork 2 --threads 2" 2 4

# Test wrapper mode with forks + threads
# check "./ddprof --live_allocations yes ./test/simple_malloc ${opts} --fork 2 --threads 2 --skip-free 100" 2 4

# Test slow profiler startup
check "env DD_PROFILING_NATIVE_STARTUP_WAIT_MS=200 ./ddprof ./test/simple_malloc ${opts}" 1

# Test switching user
if runuser -u ddbuild /usr/bin/true &> /dev/null; then
    check "./ddprof --switch_user ddbuild ./test/simple_malloc ${opts}" 1
fi
