# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

find_build() {
  # Figure out BUILD_FOLDER.  Assumes TOP_LVL_DIR is populated
  ENV_FILE=${TOP_LVL_DIR}/.env_perso.yml
  if [ ! -e ${ENV_FILE} ]; then
    ENV_FILE=${TOP_LVL_DIR}/.env.yml
  fi

  source ${TOP_LVL_DIR}/tools/yamlparser.sh
  eval $(parse_yaml "${ENV_FILE}" "env_")
  echo ${env_ddprof_directory}
}
