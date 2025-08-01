# Unless explicitly stated otherwise all files in this repository are licensed under the Apache
# License Version 2.0. This product includes software developed at Datadog
# (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# Fetch elfutilities and build them according to the compiler used in the project The following
# variables and targets are defined as result : - ELFUTILS_INCLUDE_DIRS - static lib : elf - static
# lib : dw

# Lzma / ZLib Force static library by overriding CMAKE_FIND_LIBRARY_SUFFIXES since
# FindZLIB/FindLIBLZMA don't provide the option
set(ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
find_package(BZip2)
find_package(LibLZMA)
find_package(ZLIB)
find_package(zstd)
# Restore CMAKE_FIND_LIBRARY_SUFFIXES
set(CMAKE_FIND_LIBRARY_SUFFIXES ${ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})

set(SHA256_ELF
    "616099beae24aba11f9b63d86ca6cc8d566d968b802391334c91df54eab416b4"
    CACHE STRING "SHA256 of the elfutils tar")
set(VER_ELF
    "0.192"
    CACHE STRING "elfutils version")
set(ELFUTILS_PATH ${VENDOR_PATH}/elfutils-${VER_ELF})

set(LIBDW_PATH ${ELFUTILS_PATH}/lib/libdw.a)
set(LIBELF_PATH ${ELFUTILS_PATH}/lib/libelf.a)
set(LIBEU_PATH ${ELFUTILS_PATH}/src/lib/libeu.a)

set(ELFUTILS_INCLUDE_DIRS ${ELFUTILS_PATH}/include)

if(NOT "${CMAKE_BUILD_TYPE}" STREQUAL "Release" AND NOT "${CMAKE_BUILD_TYPE}" STREQUAL
                                                    "RelWithDebInfo")
  # Variable can contain several args as it is quoted
  set(EXTRA_CFLAGS "-O0 -g")
  message(STATUS "elfutils - Adding compilation flags ${EXTRA_CFLAGS}")
endif()

message(
  STATUS
    "${CMAKE_SOURCE_DIR}/tools/fetch_elfutils.sh ${VER_ELF} ${SHA256_ELF} ${ELFUTILS_PATH} ${CMAKE_C_COMPILER} ${EXTRA_CFLAGS}"
)

execute_process(
  COMMAND "${CMAKE_SOURCE_DIR}/tools/fetch_elfutils.sh" "${VER_ELF}" "${SHA256_ELF}"
          "${ELFUTILS_PATH}" "${CMAKE_C_COMPILER}" "${EXTRA_CFLAGS}"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND_ERROR_IS_FATAL ANY)

# Define eu library first since others depend on it
add_library(eu STATIC IMPORTED)
set_target_properties(
  eu
  PROPERTIES IMPORTED_LOCATION ${LIBEU_PATH}
             INTERFACE_INCLUDE_DIRECTORIES ${ELFUTILS_INCLUDE_DIRS})

add_library(dw STATIC IMPORTED)
set_target_properties(
  dw
  PROPERTIES IMPORTED_LOCATION ${LIBDW_PATH}
             INTERFACE_INCLUDE_DIRECTORIES ${ELFUTILS_INCLUDE_DIRS}
             INTERFACE_LINK_LIBRARIES
             "eu;${BZIP2_LIBRARIES};${LIBLZMA_LIBRARIES};${ZLIB_LIBRARIES};${zstd_LIBRARIES}")

add_library(elf STATIC IMPORTED)
set_target_properties(
  elf
  PROPERTIES IMPORTED_LOCATION ${LIBELF_PATH}
             INTERFACE_INCLUDE_DIRECTORIES ${ELFUTILS_INCLUDE_DIRS}
             INTERFACE_LINK_LIBRARIES
             "${BZIP2_LIBRARIES};${LIBLZMA_LIBRARIES};${ZLIB_LIBRARIES};${zstd_LIBRARIES}")

# Elf libraries
set(ELFUTILS_LIBRARIES dw elf eu)
