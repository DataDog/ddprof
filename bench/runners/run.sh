#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

##
## // FUNCTION DECLARATION
empty_or_exit() {
  if [ $# != 0 ] && [ ${1} != "" ]; then
    echo "Error Conflicting options"
    exit 1
  fi
}

## Expect CMD / OPTION / OPTION VALUE
add_if_not_empty() {
  if [ $# == 3 ] && [ ${3:-""} != "" ]; then
    echo "${1} ${2} ${3}"
  else
    echo "${1}"
  fi
}

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


usage() {
    echo "$0 [-f configfile.yml] command arg1 arg2 ..."
    echo ""
    echo "Wrapper around ddprof to launch the tool on a given executable."
    echo "Reads a configuration file to check what ddprof / keys / parameters to use."
    echo ""
    echo "      -f : override the config file"
    echo "      -b : override build folder (ddprof folder), default is read from ${ENV_FILE} file"
    echo "      -j <N> <M>: Use jemalloc capturing samples. N and M are optional.  N=lg_prof_interval, M=lg_prof_sample "
    echo "      -env <env_field>: override the env field used to find the API key in the .env.yml (or .env_perso.yml) file"
    echo "" 
    echo "Environment variables considered"
    echo "      - DD_API_KEY: overrides the API key value picked up from the config file."
    echo ""
    echo " Mutually exclusif perf analysis options :"
    echo "      --callgrind : use callgrind"
    echo "      --massif    : use massif"
    echo "      --perfstat  : use perf with the stat option"
    echo "      --valgrind  : use valgrind"
    echo "      --ddprof    : use ddprof"
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
USE_DDPROF="no"
USE_GDB="no"
ENV_KEY="env_ddog_api_key_staging0"

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
  if [ $# != 0 ] && [ $1 == "--env" ]; then
    shift
    echo "Use key from $1"
    ENV_KEY="env_ddog_api_key_${1}"
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
  if [ $# != 0 ] && [ $1 == "--ddprof" ]; then
    shift
    USE_DDPROF="yes"
    continue
  fi
  if [ $# != 0 ] && [ $1 == "--gdb" ]; then
    shift
    USE_GDB="yes"
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
echo $config_vars
eval $config_vars

config_vars=$(parse_yaml "${ENV_FILE}" "env_")
#echo "$config_vars"
eval $config_vars

export ASAN_SYMBOLIZER_PATH=$(which llvm-symbolizer)

### Define ddprof executable
CMD="${TOP_LVL_DIR}/${env_ddprof_directory}/ddprof"

if [ ! -z ${BUILD_FOLDER} ] && [ -d ${BUILD_FOLDER} ]; then
  echo "Override ddprof folder to ${BUILD_FOLDER}..."
  CMD="${BUILD_FOLDER}/ddprof"
fi

### Define analyzers
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
  # -x allows for csv format
  PREPEND_CMD="perf stat -x ','"
  # PREPEND_CMD="perf stat" # standard display (vs csv)
fi

if [[ "yes" == "${USE_GDB,,}" ]]; then
  empty_or_exit ${PREPEND_CMD}
  PREPEND_CMD="gdb --command=$SCRIPTDIR/configs/gdb_commands.txt  --args"
fi


CMD="${PREPEND_CMD} ${CMD}"
# echo "ddprof cmd:${CMD}"

# Do service version stuff
VERFILE="tmp/run.ver"
mkdir -p $(dirname ${VERFILE})
VER=0
if [[ -f ${VERFILE} ]]; then VER=$(cat ${VERFILE}); fi
VER=$((VER+1))
echo ${VER} > ${VERFILE}

# API Key setting
if [ ! -z ${DD_API_KEY:-""} ]; then
  echo "Using env var DD_API_KEY as key"
  CMD=$(add_if_not_empty ${CMD} "-A" "${DD_API_KEY}")
else # No env var, check file for key
  CMD=$(add_if_not_empty ${CMD} "-A" ${!ENV_KEY:-""})
fi

CMD=$(add_if_not_empty ${CMD} "-I" "${cfg_ddprof_intake_site:-""}")
CMD=$(add_if_not_empty ${CMD} "-H" "${cfg_ddprof_intake_url:-""}")
CMD=$(add_if_not_empty ${CMD} "-P" "${cfg_ddprof_intake_port:-""}")
CMD=$(add_if_not_empty ${CMD} "-u" "${cfg_ddprof_upload_period:-""}")
CMD=$(add_if_not_empty ${CMD} "-E" "${cfg_ddprof_environment:-""}")
CMD=$(add_if_not_empty ${CMD} "-l" "${cfg_ddprof_loglevel:-""}")
CMD=$(add_if_not_empty ${CMD} "-e" "${cfg_ddprof_event:-""}")
CMD=$(add_if_not_empty ${CMD} "-s" "${cfg_ddprof_faultinfo:-""}")
CMD=$(add_if_not_empty ${CMD} "-w" "${cfg_ddprof_worker_period:-""}")
CMD=$(add_if_not_empty ${CMD} "-o" "${cfg_ddprof_logmode:-""}")
CMD=$(add_if_not_empty ${CMD} "-a" "${cfg_ddprof_printargs:-""}")

DDPROF_CMD=${CMD}

# Set any switchable environment variables
if [[ "yes" == "${USE_JEMALLOC,,}" ]]; then
  echo "Using jemalloc-based allocation profiling. Profile every ${JEMALLOC_INTERVALS}"
  export LD_PRELOAD="/usr/lib/x86_64-linux-gnu/libjemalloc.so"
  export MALLOC_CONF="prof:true,lg_prof_interval:${JEMALLOC_INTERVALS},lg_prof_sample:${JEMALLOC_SAMPLES},prof_prefix:jeprof.out"
fi

echo "Events: ${cfg_ddprof_event:-""}"
echo "Running: $@"

SERVICE_NAME=${cfg_ddprof_service_name:-"native_test"}
SERVICE_OPTION="-S ${SERVICE_NAME}_${VER}"

### ddprof under ddprof (with same options)
if [[ "yes" == "${USE_DDPROF,,}" ]]; then
  empty_or_exit ${PREPEND_CMD}
  SERVICE_DDPROF_OPTION="-S ddprof_profile_${VER}"
  DDPROF_CMD="${DDPROF_CMD} ${SERVICE_DDPROF_OPTION} ${DDPROF_CMD}"
fi

# Run it! (debug print, warning, can print the key)
# echo "${DDPROF_CMD} ${SERVICE_OPTION} $@"
# exit 0

eval "${DDPROF_CMD} ${SERVICE_OPTION} $@"

# Helps find the relevant trace in the UI
echo "###### Uploaded to ${cfg_ddprof_service_name}_${VER} ######"
if [[ "yes" == "${USE_DDPROF,,}" ]]; then
  echo "###### Uploaded ddprof analysis to ddprof_profile_${VER} ######"
fi
exit 0
