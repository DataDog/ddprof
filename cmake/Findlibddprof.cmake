# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# libddprof : common profiler imported libraries
## Associated https://gitlab.ddbuild.io/DataDog/libddprof-build/-/jobs/90384402
set(TAG_LIBDDPROF "v0.3.0" CACHE STRING "libddprof github tag")
set(VER_LIBDDPROF "0.3.0" CACHE STRING "libddprof version")

set(SHA256_LIBDDPROF_X86 "450ada55dc571ea17b5fdc88d5e8a425e8885365cf625b03c91e7e68b6fc113a" CACHE STRING "libddprof sha256")
set(SHA256_LIBDDPROF_ARM "25b50573189545e84e65ce66a8d834d74e031a743489af4e89f9d8e1308ed28c" CACHE STRING "libddprof sha256")

set(LIBDDPROF_ROOT ${VENDOR_PATH}/libddprof/libddprof-${CMAKE_SYSTEM_PROCESSOR}-unknown-linux-gnu)
if ( "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "aarch64" )
  set(SHA256_LIBDDPROF ${SHA256_LIBDDPROF_ARM})
elseif("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64")
  set(SHA256_LIBDDPROF ${SHA256_LIBDDPROF_X86})
else()
   message(FATAL_ERROR "Unhandled processor ${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(LIBDDPROF_REL_FFI_LIB ${LIBDDPROF_ROOT}/lib/libddprof_ffi.a)

list(APPEND
    LIBDDPROF_INCLUDE_DIR
    ${LIBDDPROF_ROOT}/include)

set(LIBDDPROF_VERSION_FILE ${LIBDDPROF_ROOT}/lib/pkgconfig/ddprof_ffi.pc)

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
