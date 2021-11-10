# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# Fetch elfutilities and build them according to the compiler used in the project
# The following variables and targets are defined as result : 
# - ELFUTILS_INCLUDE_LIST
# - static lib : elf
# - static lib : dw

set(MD5_ELF "6f58aa1b9af1a5681b1cbf63e0da2d67" CACHE STRING "md5 of the elf tar")
set(VER_ELF "0.183" CACHE STRING "elfutils version")
set(ELFUTILS_PATH ${VENDOR_PATH}/elfutils CACHE FILEPATH "location of elfutils file")

set(LIBDW_PATH ${ELFUTILS_PATH}/libdw/libdw.a CACHE FILEPATH "location of dw lib")
set(LIBELF_PATH ${ELFUTILS_PATH}/libelf/libelf.a CACHE FILEPATH "location of elf lib")
set(ELFUTILS_LIBS 
    "${LIBDW_PATH}"
    "${LIBELF_PATH}")

list(APPEND ELFUTILS_INCLUDE_LIST ${ELFUTILS_PATH} ${ELFUTILS_PATH}/libdwfl ${ELFUTILS_PATH}/libdw ${ELFUTILS_PATH}/libebl ${ELFUTILS_PATH}/libelf)

add_custom_command(OUTPUT ${ELFUTILS_LIBS}
                    COMMAND "${CMAKE_SOURCE_DIR}/tools/fetch_elfutils.sh" "${VER_ELF}" "${MD5_ELF}" ${VENDOR_PATH} ${CMAKE_C_COMPILER}
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    COMMENT "Fetching elfutils")
add_custom_target(elfutils-deps DEPENDS ${ELFUTILS_LIBS})

add_library(dw STATIC IMPORTED)
set_property(TARGET dw PROPERTY
             IMPORTED_LOCATION ${LIBDW_PATH})
add_dependencies(dw elfutils-deps)

add_library(elf STATIC IMPORTED)
set_property(TARGET elf PROPERTY
             IMPORTED_LOCATION ${LIBELF_PATH})
add_dependencies(elf elfutils-deps)
