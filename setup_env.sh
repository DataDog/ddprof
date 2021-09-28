# Run source ./setup_env.sh
export PATH=$PATH:${PWD}/tools:${PWD}/bench/runners


# Helper command lines for cmake. Source this file, then you can just do :
# SanCMake ../ 

DDPROF_INSTALL_PREFIX="../deliverables"
DDPROF_BUILD_BENCH="OFF"
NATIVE_LIB="ON"
COMMON_OPT="-DACCURACY_TEST=ON -DCMAKE_INSTALL_PREFIX=${DDPROF_INSTALL_PREFIX} -DBUILD_BENCHMARKS=${DDPROF_BUILD_BENCH} -DBUILD_NATIVE_LIB=${NATIVE_LIB}"
RelCMake() {
    cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=Release  "$@"
}

DebCMake() {
    cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=Debug "$@"
}

SanCMake() {
    cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=SanitizedDebug "$@"
}

CovCMake() {
    cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=Coverage "$@"
}

RunDDBuild() {
    # run command as ddbuild user
    su ddbuild su -s /bin/bash -c "$@" ddbuild
}
