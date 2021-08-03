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

DEFAULT_CONFIG_FILE="${TOP_LVL_DIR}/test/configs/perso.yml" 
if [ ! -e ${DEFAULT_CONFIG_FILE} ]; then
  DEFAULT_CONFIG_FILE="${TOP_LVL_DIR}/test/configs/default.yml"
fi 

# Get global configurations 
ENV_FILE=${TOP_LVL_DIR}/.env_perso.yml
if [ ! -e ${ENV_FILE} ]; then
  ENV_FILE=${TOP_LVL_DIR}/.env.yml
fi

empty_or_exit() {
  if [ $# != 0 ] && [ ${1} != "" ]; then
    echo "Error Conflicting options"
    exit 1
  fi
}

usage() {
    echo "$0 [-f configfile.yml] command arg1 arg2 ..."
    echo ""
    echo "Wrapper around ddprof to launch the tool on a given executable."
    echo "Reads a configuration file to check what ddprof / keys / parameters to use."
    echo ""
    echo "      -f : override the config file"
    echo "      -b : override build folder (ddprof folder), default is read from ${ENV_FILE} file"
    echo "      -j <N> <M>: Use jemalloc capturing samples. N and M are optional.  N=lg_prof_interval, M=lg_prof_sample "
    echo "" 
    echo " Mutually exclusif perf analysis options :"
    echo "      --callgrind : use callgrind"
    echo "      --massif : use massif"
    echo "      --perfstat : use perf with the stat option"
    echo "      --valgrind : use valgrind"
    echo ""
    echo "      keys & env : for environment (keys / ddprof version)"
    echo "                   ${ENV_FILE}"
    echo "      ddprof config : the following file is loaded by default:"
    echo "                     ${DEFAULT_CONFIG_FILE}"
}

PARAM_FOUND=1
BUILD_FOLDER=""
USE_JEMALLOC="no"

JEMALLOC_INTERVALS="26"
JEMALLOC_SAMPLES="20"

USE_MASSIF="no"
USE_CALLGRIND="no"
USE_PERFSTAT="no"
USE_VALGRIND="no"


while [ $# != 0 ] && [ ${PARAM_FOUND} == 1 ] ; do 
  if [ $# == 0 ] || [ $1 == "-h" ]; then
    usage
    exit 0
  fi

  if [ $# != 0 ] && [ $1 == "-f" ]; then
    shift
    DEFAULT_CONFIG_FILE=$1
    shift
    continue
  fi

  if [ $# != 0 ] && [ $1 == "-j" ]; then
    shift
    USE_JEMALLOC="yes"
    re='^[0-9]+$'
    JEMALLOC_INTERVALS=$1
    if ! [[ ${JEMALLOC_INTERVALS} =~ $re ]] ; then
       echo "Error: JEMALLOC_INTERVALS Not a number. Keep default"
       JEMALLOC_INTERVALS=26
    else 
      shift
    fi
    JEMALLOC_SAMPLES=$1
    if ! [[ ${JEMALLOC_SAMPLES} =~ $re ]] ; then
       echo "Error: JEMALLOC_SAMPLES Not a number. Keep default"
       JEMALLOC_SAMPLES=20
    else 
      shift
    fi
    continue
  fi
  if [ $# != 0 ] && [ $1 == "-b" ]; then
    shift
    cd $1
    BUILD_FOLDER="$PWD"
    cd $CURRENTDIR
    shift
    continue
  fi
  if [ $# != 0 ] && [ $1 == "--massif" ]; then
    shift
    USE_MASSIF="yes"
    continue
  fi

  if [ $# != 0 ] && [ $1 == "--callgrind" ]; then
    shift
    USE_CALLGRIND="yes"
    continue
  fi

  if [ $# != 0 ] && [ $1 == "--perfstat" ]; then
    shift
    USE_PERFSTAT="yes"
    continue
  fi
  if [ $# != 0 ] && [ $1 == "--valgrind" ]; then
    shift
    USE_VALGRIND="yes"
    continue
  fi
  # reach here only if we did not find params
  PARAM_FOUND=0

done


if [ ! -e ${DEFAULT_CONFIG_FILE} ]; then
  echo "Error - Unable to find ${DEFAULT_CONFIG_FILE}"
  exit 1
else 
  echo "Use config from : ${DEFAULT_CONFIG_FILE}"
fi

# Get configurations for ddprof from yml file
source ${TOP_LVL_DIR}/tools/yamlparser.sh
config_vars=$(parse_yaml "${DEFAULT_CONFIG_FILE}" "cfg_")
#echo $config_vars
eval $config_vars

config_vars=$(parse_yaml "${ENV_FILE}" "env_")
#echo "$config_vars"
eval $config_vars

PREPEND_CMD=""
if [[ "yes" == "${USE_MASSIF,,}" ]]; then
  empty_or_exit ${PREPEND_CMD}
  PREPEND_CMD="valgrind --tool=massif"
fi
if [[ "yes" == "${USE_CALLGRIND,,}" ]]; then
  empty_or_exit ${PREPEND_CMD}
  PREPEND_CMD="valgrind --tool=callgrind"
fi
if [[ "yes" == "${USE_VALGRIND,,}" ]]; then
  empty_or_exit ${PREPEND_CMD}
  PREPEND_CMD="valgrind"
fi

if [[ "yes" == "${USE_PERFSTAT,,}" ]]; then
  empty_or_exit ${PREPEND_CMD}
  PREPEND_CMD="perf stat"
fi

export ASAN_SYMBOLIZER_PATH=$(which llvm-symbolizer)

CMD="${PREPEND_CMD} ${TOP_LVL_DIR}/${env_ddprof_directory}/ddprof"


if [ ! -z ${BUILD_FOLDER} ] && [ -d ${BUILD_FOLDER} ]; then
  echo "Override ddprof folder to ${BUILD_FOLDER}..."
  CMD="${PREPEND_CMD} ${BUILD_FOLDER}/ddprof"
fi
echo "Use ddprof from : ${CMD}"

# Do service version stuff
VERFILE="tmp/run.ver"
mkdir -p $(dirname ${VERFILE})
VER=0
if [[ -f ${VERFILE} ]]; then VER=$(cat ${VERFILE}); fi
VER=$((VER+1))
echo ${VER} > ${VERFILE}

if [ ! -z ${env_ddog_api_key_staging0:-""} ]; then
  CMD="${CMD} -A ${env_ddog_api_key_staging0}"
else 
  echo "WARNING : Running without a valid API key."
fi

# Set any switchable environment variables
if [[ "yes" == "${USE_JEMALLOC,,}" ]]; then
  echo "Using jemalloc-based allocation profiling. Profile every ${JEMALLOC_INTERVALS}"
  export LD_PRELOAD="/usr/lib/x86_64-linux-gnu/libjemalloc.so"
  export MALLOC_CONF="prof:true,lg_prof_interval:${JEMALLOC_INTERVALS},lg_prof_sample:${JEMALLOC_SAMPLES},prof_prefix:jeprof.out"
fi

# Run it!
eval ${CMD} \
  -H ${cfg_ddprof_intake_url} \
  -P ${cfg_ddprof_intake_port} \
  -S "${cfg_ddprof_service_name}_${VER}"\
  -u ${cfg_ddprof_upload_period} \
  -E ${cfg_ddprof_environment}"test-staging" \
  -l ${cfg_ddprof_loglevel} \
  "$@"

# Helps find the relevant trace in the UI
echo "Uploaded to ${cfg_ddprof_service_name}_${VER}"
exit 0
