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
COMMON_OPT="${COMPILER_SETTING} -DACCURACY_TEST=ON -DCMAKE_INSTALL_PREFIX=${DDPROF_INSTALL_PREFIX} -DBUILD_BENCHMARKS=${DDPROF_BUILD_BENCH} -DBUILD_NATIVE_LIB=${NATIVE_LIB}"

GetDirectoryExtention() {
  echo "_${EXTENSION_CC,,}_${EXTENSION_OS,,}_${1}"
}

CmakeWithOptions() {
  # Build mode
  # Extra Parameters to cmake
  BUILD_TYPE=${1}
  shift
  VENDOR_EXTENSION=$(GetDirectoryExtention ${BUILD_TYPE})
  # shellcheck disable=SC2086
  cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DVENDOR_EXTENSION=${VENDOR_EXTENSION} $@
}

RelCMake() {
  BUILD_TYPE=Release
  CmakeWithOptions ${BUILD_TYPE} $@
}

DebCMake() {
    BUILD_TYPE=Debug
    CmakeWithOptions ${BUILD_TYPE} $@
}

SanCMake() {
    BUILD_TYPE=SanitizedDebug
    CmakeWithOptions ${BUILD_TYPE} $@
}

TSanCMake() {
    BUILD_TYPE=ThreadSanitizedDebug
    CmakeWithOptions ${BUILD_TYPE} $@
}

CovCMake() {
    BUILD_TYPE=Coverage
    CmakeWithOptions ${BUILD_TYPE} $@
}

## Build a directory with a naming that reflects the OS / compiler we are using
## Example : mkBuildDir Rel --> build_UB18_clang_Rel
MkBuildDir() {
    BUILD_DIR_EXTENSION=$(GetDirectoryExtention ${1})
    echo ${BUILD_DIR_EXTENSION}
    mkdir -p build${BUILD_DIR_EXTENSION} && cd "$_" || exit 1
}

RunDDBuild() {
    # run command as ddbuild user
    su ddbuild su -s /bin/bash -c "$@" ddbuild
}
