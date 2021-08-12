#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

usage() {
    echo "Launchs ddprof with a toy project and gather performance results."
    echo ""
    echo "-r record performance results"
}

if [ $# == 0 ] || [ $1 == "-h" ]; then
    usage
fi

RECORD_STATS="no"
if [ $# == 0 ] || [ $1 == "-r" ]; then
    RECORD_STATS="yes"
fi

get_cpu_from_file() {
    cat $1 | grep 'CPUs utilized' | awk -F ',' '{print $(NF-1)}'
}

get_computations_from_file() {
    cat $1 | grep 'nbComputations' | awk -F '=' '{print $2}'
}

BENCH_RUN_DURATION=30

echo "Run toy exec without profiler..."
output_prime=$(mktemp)
perf stat -x ',' BadBoggleSolver_run ${BENCH_RUN_DURATION} &> $output_prime


echo "Run profiler on toy exec..."
output_second=$(mktemp)
run.sh --perfstat BadBoggleSolver_run ${BENCH_RUN_DURATION} &> $output_second

echo "Retrieve CPU value"

CPU_PRIME=$(get_cpu_from_file ${output_prime})
CPU_SECOND=$(get_cpu_from_file ${output_second})

COMPUTATION_PRIME=$(get_computations_from_file ${output_prime})
COMPUTATION_SECOND=$(get_computations_from_file ${output_second})

echo "CPU DIFF : $CPU_PRIME vs $CPU_SECOND "
echo "COMPUTATION DIFF: $COMPUTATION_PRIME vs $COMPUTATION_SECOND"

if [ ${RECORD_STATS} == "yes" ]; then
    DATE=$(date)
    echo "BadBoggleSolver_run, ${DATE}, ${CPU_PRIME}, ${CPU_SECOND}, ${COMPUTATION_PRIME}, ${COMPUTATION_SECOND}" >> ${TOP_LVL_DIR}/test/data/perf_local_results.csv
fi
