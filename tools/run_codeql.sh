#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

### Set directory names
CURRENTDIR=$PWD
SCRIPTPATH=$(readlink -f "$0")
SCRIPTDIR=$(dirname $SCRIPTPATH)
cd $SCRIPTDIR/../
TOP_LVL_DIR=$PWD
cd $CURRENTDIR

DB_CODEQL=${TOP_LVL_DIR}/ddprof.codeql
ANALYZE_CODEQL=${TOP_LVL_DIR}/cpp-security-quality.sarif
# to be set once we fix the recommendations
NB_OF_RECOMMENDATIONS_ALLOWED=1000
RES_CODEQL=${TOP_LVL_DIR}/codeql-result.sarif

cd $TOP_LVL_DIR

if [ ! -e /usr/local/codeql/codeql ]; then
    echo "Fetching codeql"
    curl -L https://github.com/github/codeql-action/releases/download/codeql-bundle-20210622/codeql-bundle-linux64.tar.gz -o - | tar -xz -C /usr/local
fi

if [ -e ${TOP_LVL_DIR}/build_codeql ];then
    echo "${TOP_LVL_DIR}/build_codeql Dir already exists"
    echo "rm -Rf ${TOP_LVL_DIR}/build_codeql"
    exit 1
fi

if [ -e ${DB_CODEQL} ]; then
    echo "Cleaning up codeql dir... rm -Rf ${DB_CODEQL}"
    rm -Rf ${DB_CODEQL}
fi

if [ -e ${ANALYZE_CODEQL} ]; then
    echo "Cleaning up results of analysis... rm -f ${ANALYZE_CODEQL}"
    rm -f ${ANALYZE_CODEQL}
fi

/usr/local/codeql/codeql database create ${DB_CODEQL} --language="cpp" --source-root=${TOP_LVL_DIR} --command="${SCRIPTDIR}/build_codeql.sh"
/usr/local/codeql/codeql database analyze ${DB_CODEQL} /usr/local/codeql/qlpacks/codeql-cpp/codeql-suites/cpp-security-and-quality.qls --format=sarifv2.1.0 --threads=4 --output=cpp-security-quality.sarif --sarif-add-snippets

NB_SECURITY_RECOS=$(grep "snippet" cpp-security-quality.sarif | grep snippet | wc -l)

echo "####### NB Recommendations : ${NB_SECURITY_RECOS} #######"
echo "Check then out in : ${ANALYZE_CODEQL}"

RECORD_STATS="no"
# Record if we are running in CI
if [ ! -z ${CI_BUILD_ID:-""} ]; then
    RECORD_STATS="yes"
fi

if [ ${RECORD_STATS} == "yes" ]; then
  STATS_PREFIX="datadog.ci.codeql.ddprof."
  TAG_STATS="#ci_build_id:${CI_BUILD_ID}"
  SOCKET_STATSD=$(echo ${STATSD_URL} | sed 's/unix:\/\///')
  if [ -z ${SOCKET_STATSD} ];then
    echo "Socket not found... Error attempting to record statsd metrics"
    exit 1
  fi
  echo "Saving results to ${STATS_PREFIX}..."
  echo -n "${STATS_PREFIX}recommendations:${NB_SECURITY_RECOS}|g|${TAG_STATS}" | nc -U -u -w1 ${SOCKET_STATSD}
fi

if (( ${NB_SECURITY_RECOS} > ${NB_OF_RECOMMENDATIONS_ALLOWED} )); then
    echo "A new error was introduced ! Consider the analysis of codeql in ${ANALYZE_CODEQL}"
    exit 1
fi
