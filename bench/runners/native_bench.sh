#!/bin/bash

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

DDPROF_CONFIG_FILE=${TOP_LVL_DIR}/test/configs/perfanalysis.yml
RECORD_FILE=${TOP_LVL_DIR}/test/data/perf_local_results.csv
TOY_EXE="BadBoggleSolver_run"


usage() {
    echo "Launchs ddprof with a toy project and gather performance results."
    echo ""
    echo "-r record performance results (in $RECORD_FILE)"
    echo "-b <ddprof_folder> override ddprof folder."
    echo "-e <executable> override executable to benchmark."
}

RECORD_STATS="no"
BUILD_OPT=""

while getopts "b:e:hr" arg; do
  case $arg in
    b)
      BUILD_OPT="-b ${OPTARG}"
      echo "Use ddprof from : ${OPTARG}"
      ;;
    e)
      TOY_EXE="${OPTARG}"
      echo "Override TOY_EXE to : ${TOY_EXE}"
      ;;
    h)
      usage
      exit 0
      ;;
    r)
      RECORD_STATS="yes"
      ;;
  esac
done

get_cpu_from_file() {
    cat $1 | grep 'CPUs utilized' | awk -F ',' '{print $(NF-1)}'
}

get_computations_from_file() {
    cat $1 | grep 'nbComputations' | awk -F '=' '{print $2}'
}

BENCH_RUN_DURATION=20

output_prime=$(mktemp)
echo "Run toy exec without profiler... Traces recorded in $output_prime"
perf stat -x ',' ${TOY_EXE} ${BENCH_RUN_DURATION} &> $output_prime &

output_second=$(mktemp)
echo "Run profiler on toy exec... Traces recorded in $output_second"
run.sh -f ${DDPROF_CONFIG_FILE} --perfstat ${BUILD_OPT} ${TOY_EXE} ${BENCH_RUN_DURATION} &> $output_second &

echo "Wait for end of run..."
wait

check_log_errors.sh ${output_second}
retVal=$?
if [ $retVal -ne 0 ]; then
    exit 1
fi

echo "Retrieve CPU value"

CPU_PRIME=$(get_cpu_from_file ${output_prime})
CPU_SECOND=$(get_cpu_from_file ${output_second})

WORKLOAD_PRIME=$(get_computations_from_file ${output_prime})
WORKLOAD_SECOND=$(get_computations_from_file ${output_second})

echo "CPU DIFF : $CPU_PRIME vs $CPU_SECOND "
echo "WORKLOAD DIFF: $WORKLOAD_PRIME vs $WORKLOAD_SECOND"

if [ ${RECORD_STATS} == "yes" ]; then
  echo "Recording stats in ${RECORD_FILE}"
  DATE=$(date)
  echo "${TOY_EXE}, ${DATE}, ${CPU_PRIME}, ${CPU_SECOND}, ${WORKLOAD_PRIME}, ${WORKLOAD_SECOND}" >> ${TOP_LVL_DIR}/test/data/perf_local_results.csv

  # Record with statsd if socket is available
  TAG_STATS=""

  if [ ! -z ${STATSD_URL:-""} ]; then
    # Datadog.profiling is a common namespace to avoid billing customers for metrics, but in the context of this benchmark, it is less important
    # I will keep it nonetheless as a convention
    # These metrics are not part of the standard metrics exported by the profiler (as they result of this bench app)
    if [ ${TOY_EXE} == "BadBoggleSolver_run" ]; then
      TOY_STAT_NAME="" # avoid breaking current dashboards (until I have the courage to fix things)
    else 
      TOY_STAT_NAME=$(echo ${TOY_EXE} | awk -F '.' '{print $1}')
      TOY_STAT_NAME="${TOY_STAT_NAME}."
    fi
    STATS_PREFIX="datadog.profiling.native_bench.${TOY_STAT_NAME}"
    #<TAG_KEY_1>:<TAG_VALUE_1>,<TAG_2>
    if [ ! -z ${CI_BUILD_ID:-""} ]; then
      TAG_STATS="#ci_build_id:${CI_BUILD_ID}"
    fi
    echo "Saving results to ${STATS_PREFIX}..."
    SOCKET_STATSD=$(echo ${STATSD_URL} | sed 's/unix:\/\///')
    echo -n "${STATS_PREFIX}ref.cpu:${CPU_PRIME}|g|${TAG_STATS}" | nc -U -u -w1 ${SOCKET_STATSD}
    echo -n "${STATS_PREFIX}profiled.cpu:${CPU_SECOND}|g|${TAG_STATS}" | nc -U -u -w1 ${SOCKET_STATSD}
    echo -n "${STATS_PREFIX}ref.workload:${WORKLOAD_PRIME}|g|${TAG_STATS}" | nc -U -u -w1 ${SOCKET_STATSD}
    echo -n "${STATS_PREFIX}profiled.workload:${WORKLOAD_SECOND}|g|${TAG_STATS}" | nc -U -u -w1 ${SOCKET_STATSD}
  fi
fi
