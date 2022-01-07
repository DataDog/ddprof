# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# libddprof : common profiler imported libraries
## Associated https://gitlab.ddbuild.io/DataDog/libddprof-build/-/jobs/90384402
set(TAG_LIBDDPROF "v0.2.0-rc.2" CACHE STRING "libddprof github tag")
set(VER_LIBDDPROF "0.2.0" CACHE STRING "libddprof version")

set(SHA256_LIBDDPROF "7c055d11e1bcd12716fd7ff99597777c827da5dd2c99de0900f27672accb8de1" CACHE STRING "libddprof sha256")

set(LIBDDPROF_X86_ROOT ${VENDOR_PATH}/libddprof/libddprof-x86_64-unknown-linux-gnu)
set(LIBDDPROF_REL_FFI_LIB ${LIBDDPROF_X86_ROOT}/lib/libddprof_ffi.a)

list(APPEND
    LIBDDPROF_INCLUDE_DIR
    ${LIBDDPROF_X86_ROOT}/include)

set(LIBDDPROF_VERSION_FILE ${LIBDDPROF_X86_ROOT}/lib/pkgconfig/ddprof_ffi.pc)

# Expected files
set(LIBDDPROF_FILES
  ${LIBDDPROF_REL_FFI_LIB}
  ${LIBDDPROF_VERSION_FILE})

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

# This is duplicated from the cmake configuration provided by libddprof
# How to fix this : using find_package
# A find package would only work once libddprof is downloaded

add_library(ddprof-ffi STATIC IMPORTED)
set_property(TARGET ddprof-ffi PROPERTY
             IMPORTED_LOCATION ${LIBDDPROF_REL_FFI_LIB})
add_dependencies(ddprof-ffi ${LIBDDPROF_FILES} ddprof-version)

# lib util is part of libc. On newer systems, everything is within libc (lib util is an empty library for compatibility reasons)
set(LIBDDProf_LIBRARIES ddprof-ffi -ldl -lrt -lpthread -lc -lm -lrt -lpthread -lutil -ldl -lutil)

add_library(ddprof-ffi-interface INTERFACE)
target_include_directories(ddprof-ffi-interface INTERFACE ${LIBDDPROF_INCLUDE_DIR})
target_link_libraries(ddprof-ffi-interface INTERFACE ${LIBDDProf_LIBRARIES})
target_compile_features(ddprof-ffi-interface INTERFACE c_std_11)

add_library(DDProf::FFI ALIAS ddprof-ffi-interface)
