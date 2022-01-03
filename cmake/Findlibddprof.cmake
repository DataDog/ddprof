# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# libddprof : common profiler imported libraries
## Associated https://gitlab.ddbuild.io/DataDog/libddprof-build/-/jobs/90384402
set(TAG_LIBDDPROF "v0.2.0-rc.1" CACHE STRING "libddprof github tag")
set(VER_LIBDDPROF "e9b9b9c9" CACHE STRING "libddprof version")

set(SHA256_LIBDDPROF "e47d91cb3d2d73a8106417e5f5c6e94db4fc4f6f133e815c0187286d33c6be62" CACHE STRING "libddprof sha256")

set(LIBDDPROF_REL_FFI_LIB ${VENDOR_PATH}/libddprof/x86_64-unknown-linux-gnu/lib/libddprof_ffi.a)
set(LIBDDPROF_CMAKE_SCRIPT ${VENDOR_PATH}/libddprof/x86_64-unknown-linux-gnu/cmake/DDProfConfig.cmake)

list(APPEND
    LIBDDPROF_INCLUDE_DIR
    ${VENDOR_PATH}/libddprof/x86_64-unknown-linux-gnu/include)

set(LIBDDPROF_VERSION_FILE ${VENDOR_PATH}/libddprof/version.txt)

# Expected files
set(LIBDDPROF_FILES
  ${LIBDDPROF_REL_FFI_LIB}
  ${LIBDDPROF_VERSION_FILE}
  ${LIBDDPROF_CMAKE_SCRIPT})

add_custom_command(OUTPUT ${LIBDDPROF_FILES}
                  COMMAND "${CMAKE_SOURCE_DIR}/tools/fetch_libddprof.sh" ${TAG_LIBDDPROF} ${SHA256_LIBDDPROF} ${VENDOR_PATH}
                  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                  COMMENT "Fetching libddprof version ${TAG_LIBDDPROF}...")
add_custom_target(ddprof-deps DEPENDS ${LIBDDPROF_FILES})

# Version check 
# The dependency is downloaded as a build cache in the vendor directory
# When importing the library, also check the version to make sure we are coherent (lib can be fetched in previous builds)
# A file is created in the build dir to detect when to re-run the check
# Warning : CMakeCache.txt will hold variables like VER_LIBDDPROF. Clean it if you want to update versions (or clean build tree)
set(LIBDDPROF_VERSION_CHECK_FILE ${CMAKE_BINARY_DIR}/libddprof_version_check.txt)
add_custom_command(OUTPUT ${LIBDDPROF_VERSION_CHECK_FILE}
                   COMMAND "${CMAKE_SOURCE_DIR}/tools/check_libddprof_version.sh" ${LIBDDPROF_VERSION_FILE} ${VER_LIBDDPROF} ${LIBDDPROF_VERSION_CHECK_FILE}
                   DEPENDS ddprof-deps
                   ARGS ${LIBDDPROF_VERSION_FILE})
add_custom_target(ddprof-version DEPENDS ${LIBDDPROF_VERSION_CHECK_FILE})

# To be replaced by the import of the cmake script
add_library(ddprof-ffi STATIC IMPORTED)
set_property(TARGET ddprof-ffi PROPERTY
             IMPORTED_LOCATION ${LIBDDPROF_REL_FFI_LIB})
add_dependencies(ddprof-ffi ddprof-version)
add_library(DDProf::FFI ALIAS ddprof-ffi)
