# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# libddprof : common profiler imported libraries
## Associated https://gitlab.ddbuild.io/DataDog/libddprof-build/-/jobs/90384402
set(TAG_LIBDDPROF "v0.5.0-rc.1" CACHE STRING "libddprof github tag")

set(SHA256_LIBDDPROF_X86 "2db92e2ad87005a043e415fd62079af1f1df3642be9bed3ade840c5533a61063" CACHE STRING "libddprof sha256")
set(SHA256_LIBDDPROF_ARM "ab4cd1fc9bc3975775bd2ff2122ac0b475533d504965b931d377cca122f7b0b3" CACHE STRING "libddprof sha256")

set(LIBDDPROF_ROOT ${VENDOR_PATH}/libddprof-${TAG_LIBDDPROF})
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

execute_process(COMMAND "${CMAKE_SOURCE_DIR}/tools/fetch_libddprof.sh" ${TAG_LIBDDPROF} ${SHA256_LIBDDPROF} ${LIBDDPROF_ROOT}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND_ERROR_IS_FATAL ANY)

# This is duplicated from the cmake configuration provided by libddprof
# How to fix this : using find_package
# A find package would only work once libddprof is downloaded

add_library(ddprof-ffi STATIC IMPORTED)
set_property(TARGET ddprof-ffi PROPERTY
             IMPORTED_LOCATION ${LIBDDPROF_REL_FFI_LIB})

# lib util is part of libc. On newer systems, everything is within libc (lib util is an empty library for compatibility reasons)
set(LIBDDProf_LIBRARIES ddprof-ffi -ldl -lrt -lpthread -lc -lm -lrt -lpthread -lutil -ldl -lutil)

add_library(ddprof-ffi-interface INTERFACE)
target_include_directories(ddprof-ffi-interface INTERFACE ${LIBDDPROF_INCLUDE_DIR})
target_link_libraries(ddprof-ffi-interface INTERFACE ${LIBDDProf_LIBRARIES})
target_compile_features(ddprof-ffi-interface INTERFACE c_std_11)

add_library(DDProf::FFI ALIAS ddprof-ffi-interface)
