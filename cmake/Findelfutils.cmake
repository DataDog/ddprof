# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# Fetch elfutilities and build them according to the compiler used in the project
# The following variables and targets are defined as result : 
# - ELFUTILS_INCLUDE_LIST
# - static lib : elf
# - static lib : dw

set(SHA256_ELF "7f6fb9149b1673d38d9178a0d3e0fb8a1ec4f53a9f4c2ff89469609879641177" CACHE STRING "SHA256 of the elf tar")
set(VER_ELF "0.186" CACHE STRING "elfutils version")
set(ELFUTILS_PATH ${VENDOR_PATH}/elfutils-${VER_ELF})

set(LIBDW_PATH ${ELFUTILS_PATH}/lib/libdw.a)
set(LIBELF_PATH ${ELFUTILS_PATH}/lib/libelf.a)

list(APPEND ELFUTILS_INCLUDE_LIST ${ELFUTILS_PATH}/include)

execute_process(COMMAND "${CMAKE_SOURCE_DIR}/tools/fetch_elfutils.sh" "${VER_ELF}" "${SHA256_ELF}" "${ELFUTILS_PATH}" "${CMAKE_C_COMPILER}"
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_library(dw STATIC IMPORTED)
set_property(TARGET dw PROPERTY
             IMPORTED_LOCATION ${LIBDW_PATH})

add_library(elf STATIC IMPORTED)
set_property(TARGET elf PROPERTY
             IMPORTED_LOCATION ${LIBELF_PATH})
