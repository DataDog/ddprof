# Unless explicitly stated otherwise all files in this repository are licensed under the Apache
# License Version 2.0. This product includes software developed at Datadog
# (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# libdatadog : common profiler imported libraries
# https://github.com/DataDog/libdatadog/releases/tag/v7.0.0
set(TAG_LIBDATADOG
    "v14.3.1"
    CACHE STRING "libdatadog github tag")

set(Datadog_ROOT ${VENDOR_PATH}/libdatadog-${TAG_LIBDATADOG})

message(STATUS "${CMAKE_SOURCE_DIR}/tools/fetch_libddprof.sh ${TAG_LIBDATADOG} ${LIBDATADOG_ROOT}")
execute_process(
  COMMAND "${CMAKE_SOURCE_DIR}/tools/fetch_libdatadog.sh" ${TAG_LIBDATADOG} ${Datadog_ROOT}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND_ERROR_IS_FATAL ANY)

set(DataDog_DIR "${Datadog_ROOT}/cmake")

# Prefer static library to shared library
set(CMAKE_FIND_LIBRARY_SUFFIXES_BACKUP ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES .a .so)

find_package(Datadog REQUIRED)

# Restore CMAKE_FIND_LIBRARY_SUFFIXES
set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES_BACKUP})
