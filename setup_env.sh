# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# Run source ./setup_env.sh
export PATH=$PATH:${PWD}/tools:${PWD}/bench/runners

# Helper command lines for cmake. Source this file, then you can just do :
# SanCMake ../ 

DDPROF_INSTALL_PREFIX="../deliverables"
DDPROF_BUILD_BENCH="ON"
NATIVE_LIB="ON"
COMPILER_SETTING="-DCMAKE_CXX_COMPILER=${CXX:-"clang++"} -DCMAKE_C_COMPILER=${CC:-"clang"}"
# Avoid having the vendors compiled in the same directory
VENDOR_EXTENSION="-DVENDOR_EXTENSION=_${CC:-,,}_${OS_IDENTIFIER:-,,}"
COMMON_OPT="${COMPILER_SETTING} ${VENDOR_EXTENSION} -DACCURACY_TEST=ON -DCMAKE_INSTALL_PREFIX=${DDPROF_INSTALL_PREFIX} -DBUILD_BENCHMARKS=${DDPROF_BUILD_BENCH} -DBUILD_NATIVE_LIB=${NATIVE_LIB}"

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

mkBuildDir() {
    mkdir -p build_${OS_IDENTIFIER}_${CC}_${1}
    cd build_${OS_IDENTIFIER}_${CC}_${1}
}

RunDDBuild() {
    # run command as ddbuild user
    su ddbuild su -s /bin/bash -c "$@" ddbuild
}
