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
