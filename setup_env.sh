# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# Run source ./setup_env.sh
export PATH=$PATH:${PWD}/tools:${PWD}/bench/runners

# Helper command lines for cmake. Source this file, then you can just do :
# SanCMake ../ 

DDPROF_INSTALL_PREFIX="../deliverables"
DDPROF_BUILD_BENCH="ON"
NATIVE_LIB="ON"
COMPILER_SETTING="-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang"
COMMON_OPT="${COMPILER_SETTING} -DACCURACY_TEST=ON -DCMAKE_INSTALL_PREFIX=${DDPROF_INSTALL_PREFIX} -DBUILD_BENCHMARKS=${DDPROF_BUILD_BENCH} -DBUILD_NATIVE_LIB=${NATIVE_LIB}"

RelCMake() {
    cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=Release  "$@"
}

DebCMake() {
    cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=Debug "$@"
}

SanCMake() {
    cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=SanitizedDebug "$@"
}

TSanCMake() {
    cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=ThreadSanitizedDebug "$@"
}

CovCMake() {
    cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=Coverage "$@"
}

RunDDBuild() {
    # run command as ddbuild user
    su ddbuild su -s /bin/bash -c "$@" ddbuild
}
