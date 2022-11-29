# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

echoerr() { echo "$@" 1>&2; }

# Run source ./setup_env.sh
export PATH=$PATH:${PWD}/tools:${PWD}/bench/runners

# Helper command lines for cmake. Source this file, then you can just do :
# SanCMake ../ 

# Attempt to use the explicit latest known working version of GCC as default
DDPROF_CC_DEFAULT=gcc
DDPROF_CXX_DEFAULT=g++
for cc_ver in gcc-{12..9}; do
  if command -v "$cc_ver" > /dev/null; then
    DDPROF_CC_DEFAULT="$cc_ver"
    break
  fi
done

for cxx_ver in g++-{12..9}; do
  if command -v "$cxx_ver" > /dev/null; then
    DDPROF_CXX_DEFAULT="$cxx_ver"
    break
  fi
done

echoerr "Using DDPROF_CXX_DEFAULT=${DDPROF_CXX_DEFAULT}"
echoerr "Using DDPROF_CC_DEFAULT=${DDPROF_CC_DEFAULT}"
echoerr "Compiler can be overriden with CXX and CC variables when sourcing ${0}"

SCRIPTDIR="$(cd -- $( dirname -- "${BASH_SOURCE[0]}" ) && pwd)" # no "$0" when sourcing
DDPROF_INSTALL_PREFIX="../deliverables"
DDPROF_BUILD_BENCH="ON"
<<<<<<< HEAD
COMPILER_SETTING="-DCMAKE_CXX_COMPILER=\"${CXX:-${DDPROF_CXX_DEFAULT}}\" -DCMAKE_C_COMPILER=\"${CC:-${DDPROF_CC_DEFAULT}}\""

echoerr "Compiler can be overriden with CXX and CC variables when sourcing this script. Current value:"
echoerr "${COMPILER_SETTING}"

=======
NATIVE_LIB="ON"
COMPILER_SETTING="-DCMAKE_CXX_COMPILER=\"${CXX:-${DDPROF_CXX_DEFAULT}}\" -DCMAKE_C_COMPILER=\"${CC:-${DDPROF_CC_DEFAULT}}\""

>>>>>>> f562ce9 (Minor fix for zsh)
# Avoid having the vendors compiled in the same directory
DDPROF_EXTENSION_CC=${CC:-"gcc"}
# strip version number from compiler
DDPROF_EXTENSION_CC=${DDPROF_EXTENSION_CC%-*}

LIBC_HAS_MUSL=$(ldd  --version 2>&1  | grep musl || true)
if [ ! -z "${LIBC_HAS_MUSL}" ]; then
  DDPROF_LIBC_VERSION=$(${SCRIPTDIR}/tools/get_libc_version.sh musl)
  DDPROF_EXTENSION_OS="alpine-linux-${DDPROF_LIBC_VERSION}"
else
  DDPROF_LIBC_VERSION=$(${SCRIPTDIR}/tools/get_libc_version.sh gnu)
  DDPROF_EXTENSION_OS="unknown-linux-${DDPROF_LIBC_VERSION}"
fi

DEFAULT_ALLOCATOR_OPT="-DDDPROF_ALLOCATOR=JEMALLOC"

GetDefaultAllocatorOptions() {
  echo ${DEFAULT_ALLOCATOR_OPT}
}

GetDirectoryExtention() {
<<<<<<< HEAD
  echo "_${DDPROF_EXTENSION_CC}_${DDPROF_EXTENSION_OS}_${1}"
=======
  echo "_${EXTENSION_CC}_${EXTENSION_OS}_${1}"
>>>>>>> f562ce9 (Minor fix for zsh)
}

COMMON_OPT="${COMPILER_SETTING} ${DEFAULT_ALLOCATOR_OPT} -DCMAKE_INSTALL_PREFIX=${DDPROF_INSTALL_PREFIX} -DBUILD_BENCHMARKS=${DDPROF_BUILD_BENCH}"

# echoerr "Cmake settings--\n ${COMMON_OPT}"

# echoerr "Cmake settings--\n ${COMMON_OPT}"

CmakeWithOptions() {
  # Build mode
  # Extra Parameters to cmake
  local BUILD_TYPE=${1}
  shift
  local VENDOR_EXTENSION=$(GetDirectoryExtention ${BUILD_TYPE})
  # shellcheck disable=SC2086
  cmake_cmd="cmake ${COMMON_OPT} -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DVENDOR_EXTENSION=${VENDOR_EXTENSION} $@"
  echoerr "-------------- cmake command -------------- "
  echoerr ${cmake_cmd}
  eval ${cmake_cmd}
}

RelCMake() {
  local BUILD_TYPE=Release
  CmakeWithOptions ${BUILD_TYPE} $@
}

DebCMake() {
    local BUILD_TYPE=Debug
    CmakeWithOptions ${BUILD_TYPE} $@
}

SanCMake() {
    local BUILD_TYPE=SanitizedDebug
    CmakeWithOptions ${BUILD_TYPE} $@
}

TSanCMake() {
    local BUILD_TYPE=ThreadSanitizedDebug
    CmakeWithOptions ${BUILD_TYPE} $@
}

CovCMake() {
    local BUILD_TYPE=Coverage
    CmakeWithOptions ${BUILD_TYPE} $@
}

## Build a directory with a naming that reflects the OS / compiler we are using
## Example : mkBuildDir Rel --> build_UB18_clang_Rel
MkBuildDir() {
    local BUILD_DIR_EXTENSION=$(GetDirectoryExtention ${1})
    echo ${BUILD_DIR_EXTENSION}
    mkdir -p build${BUILD_DIR_EXTENSION} && cd "$_" || exit 1
}

RunDDBuild() {
    # run command as ddbuild user
    su ddbuild su -s /bin/bash -c "$@" ddbuild
}
