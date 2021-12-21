# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

SET(GCC_DEBUG_FLAGS "-g -Wall")
set(SAN_FLAGS "-fsanitize=undefined -fsanitize=float-divide-by-zero -fno-sanitize-recover")
set(ASAN_FLAGS "-fsanitize=address -fsanitize=leak")
set(TSAN_FLAGS "-fsanitize=thread")
set(STACK_FLAGS "-fstack-protector-all")
## Frame pointers
set(FRAME_PTR_FLAG "-fno-omit-frame-pointer")

list(APPEND CMAKE_CONFIGURATION_TYPES SanitizedDebug ThreadSanitizedDebug)

# Add new build types
message(STATUS "Adding build types...")

## Add flags for sanitized debug (asan)
SET(CMAKE_CXX_FLAGS_SANITIZEDDEBUG
    "${GCC_DEBUG_FLAGS} ${SAN_FLAGS} ${ASAN_FLAGS} ${STACK_FLAGS}"
    CACHE STRING "Flags used by the C++ compiler during sanitized builds."
    FORCE )
SET(CMAKE_C_FLAGS_SANITIZEDDEBUG
    "${GCC_DEBUG_FLAGS} ${SAN_FLAGS} ${ASAN_FLAGS} ${STACK_FLAGS}"
    CACHE STRING "Flags used by the C compiler during sanitized builds."
    FORCE )
SET(CMAKE_EXE_LINKER_FLAGS_SANITIZEDDEBUG
    ""
    CACHE STRING "Flags used for linking binaries during sanitized builds."
    FORCE )
SET(CMAKE_SHARED_LINKER_FLAGS_SANITIZEDDEBUG
    ""
    CACHE STRING "Flags used by the shared libraries linker during sanitized builds."
    FORCE )

## Add flags for thread-sanized debu
SET(CMAKE_CXX_FLAGS_THREADSANITIZEDDEBUG
    "${GCC_DEBUG_FLAGS} ${SAN_FLAGS} ${TSAN_FLAGS} ${STACK_FLAGS}"
    CACHE STRING "Flags used by the C++ compiler during sanitized builds."
    FORCE )
SET(CMAKE_C_FLAGS_THREADSANITIZEDDEBUG
    "${GCC_DEBUG_FLAGS} ${SAN_FLAGS} ${TSAN_FLAGS} ${STACK_FLAGS}"
    CACHE STRING "Flags used by the C compiler during sanitized builds."
    FORCE )
SET(CMAKE_EXE_LINKER_FLAGS_THREADSANITIZEDDEBUG
    ""
    CACHE STRING "Flags used for linking binaries during sanitized builds."
    FORCE )
SET(CMAKE_SHARED_LINKER_FLAGS_THREADSANITIZEDDEBUG
    ""
    CACHE STRING "Flags used by the shared libraries linker during sanitized builds."
    FORCE )

string (REPLACE ";" " " LD_FLAGS_STR "${LD_FLAGS}")


if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # In clang static link is the default
    # lsan is combined with asan
    # CMake: Avoid usage of list to make sure we have spaces (not ;)
    # static ubsan is giving link errors : to be investigated
    set(CMAKE_EXE_LINKER_FLAGS_SANITIZEDDEBUG "${CMAKE_EXE_LINKER_FLAGS_SANITIZEDDEBUG} -static-libasan -static-libubsan")
    set(CMAKE_SHARED_LINKER_FLAGS_SANITIZEDDEBUG "${CMAKE_SHARED_LINKER_FLAGS_SANITIZEDDEBUG} -static-libasan -static-libubsan")
    set(CMAKE_EXE_LINKER_FLAGS_THREADSANITIZEDDEBUG "${CMAKE_EXE_LINKER_FLAGS_THREADSANITIZEDDEBUG} -static-libtsan -static-libubsan")
    set(CMAKE_SHARED_LINKER_FLAGS_THREADSANITIZEDDEBUG "${CMAKE_SHARED_LINKER_FLAGS_THREADSANITIZEDDEBUG} -static-libtsan -static-libubsan")
endif()

MARK_AS_ADVANCED(
    CMAKE_CXX_FLAGS_SANITIZEDDEBUG
    CMAKE_C_FLAGS_SANITIZEDDEBUG
    CMAKE_EXE_LINKER_FLAGS_SANITIZEDDEBUG
    CMAKE_SHARED_LINKER_FLAGS_SANITIZEDDEBUG
    CMAKE_CXX_FLAGS_THREADSANITIZEDDEBUG
    CMAKE_C_FLAGS_THREADSANITIZEDDEBUG
    CMAKE_EXE_LINKER_FLAGS_THREADSANITIZEDDEBUG
    CMAKE_SHARED_LINKER_FLAGS_THREADSANITIZEDDEBUG )

list(APPEND CMAKE_CONFIGURATION_TYPES Coverage)

# Add new build types
SET(CMAKE_CXX_FLAGS_COVERAGE
    "${GCC_DEBUG_FLAGS} -fprofile-arcs -ftest-coverage"
    CACHE STRING "Flags used by the C++ compiler during coverage builds."
    FORCE )
SET(CMAKE_C_FLAGS_COVERAGE
    "${GCC_DEBUG_FLAGS} -fprofile-arcs -ftest-coverage"
    CACHE STRING "Flags used by the C compiler during coverage builds."
    FORCE )
SET(CMAKE_EXE_LINKER_FLAGS_COVERAGE
    ""
    CACHE STRING "Flags used for linking binaries during coverage builds."
    FORCE )
SET(CMAKE_SHARED_LINKER_FLAGS_COVERAGE
    ""
    CACHE STRING "Flags used by the shared libraries linker during coverage builds."
    FORCE )

MARK_AS_ADVANCED(
    CMAKE_CXX_FLAGS_COVERAGE
    CMAKE_C_FLAGS_COVERAGE
    CMAKE_EXE_LINKER_FLAGS_COVERAGE
    CMAKE_SHARED_LINKER_FLAGS_COVERAGE )
