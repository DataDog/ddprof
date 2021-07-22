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

usage() {
    echo "$0 [-f configfile.yml] command arg1 arg2 ..."
    echo ""
    echo "Wrapper around ddprof to launch the tool on a given executable."
    echo "Reads a configuration file to check what ddprof / keys / parameters to use."
    echo ""
    echo "      -f : override the config file"
    echo "      -b : override build folder (ddprof folder), default is in .env file"
    echo ""
    echo "      keys & env : for environment (keys / ddprof version)"
    echo "                   ${ENV_FILE}"
    echo "      ddprof config : the following file is loaded by default:"
    echo "                     ${DEFAULT_CONFIG_FILE}"
}

PARAM_FOUND=1
BUILD_FOLDER=""
while [ $# != 0 ] && [ ${PARAM_FOUND} == 1 ] ; do 
  if [ $# == 0 ] || [ $1 == "-h" ]; then
    usage
    exit 0
  fi

  # Parse parameters 
  if [ $# != 0 ] && [ $1 == "-f" ]; then
    shift
    DEFAULT_CONFIG_FILE=$1
    shift
    PARAM_FOUND=1
    continue
  fi

  # Parse parameters 
  if [ $# != 0 ] && [ $1 == "-b" ]; then
    shift
    cd $1
    BUILD_FOLDER="$PWD"
    cd $CURRENTDIR
    shift
    PARAM_FOUND=1
    continue
  fi
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

export ASAN_SYMBOLIZER_PATH=$(which llvm-symbolizer)
CMD="${TOP_LVL_DIR}/${env_ddprof_directory}/ddprof"

if [ ! -z ${BUILD_FOLDER} ] && [ -d ${BUILD_FOLDER} ]; then
  echo "Override ddprof folder to ${BUILD_FOLDER}..."
  CMD="${BUILD_FOLDER}/ddprof"
fi

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


# Run it!
eval ${CMD} \
  -H ${cfg_ddprof_intake_url} \
  -P ${cfg_ddprof_intake_port} \
  -S "${cfg_ddprof_service_name}_${VER}"\
  -u ${cfg_ddprof_upload_period} \
  -E ${cfg_ddprof_environment}"test-staging" \
  -l ${cfg_ddprof_loglevel} \
  "$@"

exit 0
