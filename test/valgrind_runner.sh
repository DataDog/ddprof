#!/usr/bin/env bash

usage() {
  echo "Usage: $0 -c CONFIG_FILE [-d DDPROF_EXECUTABLE] -- [additional profiling args]"
  echo "   -c    path to configuration file"
  echo "   -d    (optional) path to ddprof executable (default is './ddprof')"

  echo "   -h    display help"
  exit 1
}

check_valgrind_errors() {
  ERRORS_VALGRIND=$(grep "ERROR SUMMARY:" ${1} )
  # Check if we have some errors flagged in report
  REMAINING_ERRORS=$(echo $ERRORS_VALGRIND | awk '{if($0!~"0 errors"){print $0}}')
  if [ ! -z ${REMAINING_ERRORS,,} ]; then
    echo "---------- Error checking valgrind report ----------"
    echo ${ERRORS_VALGRIND}
    cat $1
    exit 1
  fi
  echo "Parsing ${1}"
  # Select all regions that start with { and end with } (these match valgrind suppressions)
  SUPPRESSIONS_VALGRIND=$(awk 'BEGIN{ok=1} \
      {if($0~"^\{"){ok=0;} if(ok==0){print $0;} if($0~"^\}"){ok=1;} }' ${1})
  if [ "${SUPPRESSIONS_VALGRIND,,}" != "" ]; then
    # A new leak must have been detected --> error out
    echo "---------- Error checking new suppressions ----------"
    echo ${SUPPRESSIONS_VALGRIND}
    exit 1
  fi
  echo "Success! No new failures were found in ${ERRORS_VALGRIND}"
}

DDPROF_EXECUTABLE="./ddprof"
VALGRIND_SUPPRESSION="../test/data/valgrind_suppression_release.supp"
while getopts ":c:d:h:s" opt; do
  case ${opt} in
    c) CONFIG_FILE="${OPTARG}" ;;
    d) DDPROF_EXECUTABLE="${OPTARG}" ;;
    s) VALGRIND_SUPPRESSION="$OPTARG" ;;
    h) usage ;;
    \?) echo "Invalid option: -$OPTARG" 1>&2 ; usage ;;
    :) echo "Option -$OPTARG requires an argument" 1>&2 ; usage ;;
  esac
done
shift $((OPTIND -1))

if [ ! -e "${VALGRIND_SUPPRESSION}" ]; then
  echo "Please specify a valgrind suppression file"
fi
if [ -z "${CONFIG_FILE}" ]; then
  echo "Config file must be provided" ; usage
fi

TARGET_PROFILING=$@

TOP_LEVEL_DDPROF="/home/r1viollet/dd/ddprof_2"
OUTPUT_VALGRIND=$(mktemp)

PREPEND_CMD="valgrind --leak-check=full --show-reachable=yes --error-limit=no --gen-suppressions=all --log-file=${OUTPUT_VALGRIND} --suppressions=${VALGRIND_SUPPRESSION}"

CMD="${PREPEND_CMD} ${DDPROF_EXECUTABLE} --config ${CONFIG_FILE} ${TARGET_PROFILING}"

echo ${CMD}
eval ${CMD}

check_valgrind_errors ${OUTPUT_VALGRIND}
