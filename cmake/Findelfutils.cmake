# Unless explicitly stated otherwise all files in this repository are licensed under the Apache
# License Version 2.0. This product includes software developed at Datadog
# (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# Fetch elfutilities and build them according to the compiler used in the project The following
# variables and targets are defined as result : - ELFUTILS_INCLUDE_DIRS - static lib : elf - static
# lib : dw

# Lzma / ZLib Force static library by overriding CMAKE_FIND_LIBRARY_SUFFIXES since
# FindZLIB/FindLIBLZMA don't provide the option
set(ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".so")
find_package(LibLZMA)
find_package(ZLIB)
# Restore CMAKE_FIND_LIBRARY_SUFFIXES
set(CMAKE_FIND_LIBRARY_SUFFIXES ${ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})

set(SHA512_ELF
    "585551b2d937d19d1becfc2f28935db1dd1a3d25571a62f322b70ac8da98c1a741a55d070327705df6c3e2ee026652e0b9a3c733b050a0b0ec5f2fc75d5b74b5"
    CACHE STRING "SHA512 of the elfutils tar")
set(VER_ELF
    "0.188"
    CACHE STRING "elfutils version")
set(ELFUTILS_PATH ${VENDOR_PATH}/elfutils-${VER_ELF})

set(LIBDW_PATH ${ELFUTILS_PATH}/lib/libdw.a)
set(LIBELF_PATH ${ELFUTILS_PATH}/lib/libelf.a)

set(ELFUTILS_INCLUDE_DIRS ${ELFUTILS_PATH}/include)

if(NOT "${CMAKE_BUILD_TYPE}" STREQUAL "Release" AND NOT "${CMAKE_BUILD_TYPE}" STREQUAL
                                                    "RelWithDebInfo")
  # Variable can contain several args as it is quoted
  set(EXTRA_CFLAGS "-O0 -g")
  message(STATUS "elfutils - Adding compilation flags ${EXTRA_CFLAGS}")
endif()

message(
  STATUS
    "${CMAKE_SOURCE_DIR}/tools/fetch_elfutils.sh ${VER_ELF} ${SHA512_ELF} ${ELFUTILS_PATH} ${CMAKE_C_COMPILER} ${EXTRA_CFLAGS}"
)

execute_process(
  COMMAND "${CMAKE_SOURCE_DIR}/tools/fetch_elfutils.sh" "${VER_ELF}" "${SHA512_ELF}"
          "${ELFUTILS_PATH}" "${CMAKE_C_COMPILER}" "${EXTRA_CFLAGS}"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND_ERROR_IS_FATAL ANY)

add_library(dw STATIC IMPORTED)
set_target_properties(
  dw
  PROPERTIES IMPORTED_LOCATION ${LIBDW_PATH}
             INTERFACE_INCLUDE_DIRECTORIES ${ELFUTILS_INCLUDE_DIRS}
             INTERFACE_LINK_LIBRARIES "${LIBLZMA_LIBRARIES};${ZLIB_LIBRARIES}")

add_library(elf STATIC IMPORTED)
set_target_properties(
  elf
  PROPERTIES IMPORTED_LOCATION ${LIBELF_PATH}
             INTERFACE_INCLUDE_DIRECTORIES ${ELFUTILS_INCLUDE_DIRS}
             INTERFACE_LINK_LIBRARIES "${LIBLZMA_LIBRARIES};${ZLIB_LIBRARIES}")

# Elf libraries
set(ELFUTILS_LIBRARIES dw elf)
