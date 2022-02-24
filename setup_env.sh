# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# Run source ./setup_env.sh
export PATH=$PATH:${PWD}/tools:${PWD}/bench/runners

# Helper command lines for cmake. Source this file, then you can just do :
# SanCMake ../ 

DDPROF_INSTALL_PREFIX="../deliverables"
DDPROF_BUILD_BENCH="ON"
NATIVE_LIB="ON"
COMPILER_SETTING="-DCMAKE_CXX_COMPILER=${CXX:-"g++"} -DCMAKE_C_COMPILER=${CC:-"gcc"}"
# Avoid having the vendors compiled in the same directory
EXTENSION_CC=${CC:-"gcc"}
# strip version number from compiler
EXTENSION_CC=${EXTENSION_CC%-*}
EXTENSION_OS=${OS_IDENTIFIER:-"linux"}
VENDOR_EXTENSION="-DVENDOR_EXTENSION=_${EXTENSION_CC,,}_${EXTENSION_OS,,}"
COMMON_OPT="${COMPILER_SETTING} ${VENDOR_EXTENSION} -GNinja -DACCURACY_TEST=ON -DCMAKE_INSTALL_PREFIX=${DDPROF_INSTALL_PREFIX} -DBUILD_BENCHMARKS=${DDPROF_BUILD_BENCH} -DBUILD_NATIVE_LIB=${NATIVE_LIB}"

RelCMake() {
    # shellcheck disable=SC2086
    cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=Release  "$@"
}

DebCMake() {
    # shellcheck disable=SC2086
    cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=Debug "$@"
}

SanCMake() {
    # shellcheck disable=SC2086
    cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=SanitizedDebug "$@"
}

TSanCMake() {
    # shellcheck disable=SC2086
    cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=ThreadSanitizedDebug "$@"
}

CovCMake() {
    # shellcheck disable=SC2086
    cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=Coverage "$@"
}

## Build a directory with a naming that reflects the OS / compiler we are using
## Example : mkBuildDir Rel --> build_UB18_clang_Rel
MkBuildDir() {
    mkdir -p "build_${OS_IDENTIFIER}_${CC}_${1}" && cd "$_" || exit 1
}

RunDDBuild() {
    # run command as ddbuild user
    su ddbuild su -s /bin/bash -c "$@" ddbuild
}
