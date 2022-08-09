# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# libdatadog : common profiler imported libraries
# https://github.com/DataDog/libdatadog/releases/tag/v0.7.0-rc.1
set(TAG_LIBDDPROF "v0.7.0-rc.1" CACHE STRING "libdatadog github tag")

set(LIBDDPROF_ROOT ${VENDOR_PATH}/libddprof-${TAG_LIBDDPROF})
set(LIBDDPROF_REL_FFI_LIB ${LIBDDPROF_ROOT}/lib/libddprof_ffi.a)

list(APPEND
    LIBDDPROF_INCLUDE_DIR
    ${LIBDDPROF_ROOT}/include)

message(STATUS "${CMAKE_SOURCE_DIR}/tools/fetch_libddprof.sh ${TAG_LIBDDPROF} ${LIBDDPROF_ROOT}")
execute_process(COMMAND "${CMAKE_SOURCE_DIR}/tools/fetch_libddprof.sh" ${TAG_LIBDDPROF} ${LIBDDPROF_ROOT}
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
