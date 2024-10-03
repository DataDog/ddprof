# Unless explicitly stated otherwise all files in this repository are licensed under the Apache
# License Version 2.0. This product includes software developed at Datadog
# (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# Default built type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE
      "Release"
      CACHE STRING "CMake build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)

add_compile_options(-Wall -g -fno-semantic-interposition -fvisibility=hidden)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 7.0)
    add_compile_options(-Wshadow=local)
  endif()
else()
  add_compile_options(-Wshadow -Wno-c99-designator -fsized-deallocation)
endif()

set(SAN_FLAGS "-fsanitize=undefined -fsanitize=float-divide-by-zero -fno-sanitize-recover")
set(ASAN_FLAGS "-fsanitize=address")
set(TSAN_FLAGS "-fsanitize=thread")
set(STACK_FLAGS "-fstack-protector-strong")

# Frame pointers
set(FRAME_PTR_FLAG "-fno-omit-frame-pointer")

list(APPEND CMAKE_CONFIGURATION_TYPES SanitizedDebug ThreadSanitizedDebug)

# Add new build types
message(STATUS "Adding build types...")

# Add flags for sanitized debug (asan)
set(CMAKE_CXX_FLAGS_SANITIZEDDEBUG
    "${SAN_FLAGS} ${ASAN_FLAGS} ${STACK_FLAGS}"
    CACHE STRING "Flags used by the C++ compiler during sanitized builds." FORCE)
set(CMAKE_C_FLAGS_SANITIZEDDEBUG
    "${SAN_FLAGS} ${ASAN_FLAGS} ${STACK_FLAGS}"
    CACHE STRING "Flags used by the C compiler during sanitized builds." FORCE)

# Add flags for thread-sanized debu
set(CMAKE_CXX_FLAGS_THREADSANITIZEDDEBUG
    "${SAN_FLAGS} ${TSAN_FLAGS} ${STACK_FLAGS}"
    CACHE STRING "Flags used by the C++ compiler during sanitized builds." FORCE)
set(CMAKE_C_FLAGS_THREADSANITIZEDDEBUG
    "${SAN_FLAGS} ${TSAN_FLAGS} ${STACK_FLAGS}"
    CACHE STRING "Flags used by the C compiler during sanitized builds." FORCE)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} --print-runtime-dir
    OUTPUT_VARIABLE LLVM_RUNTIME_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  # In clang static link is the default and lsan is combined with asan. Avoid usage of list to make
  # sure we have spaces (not ;). static ubsan is giving link errors : to be investigated...
  # compiler-rt is required on aarch64 for __muloti4
  set(CMAKE_EXE_LINKER_FLAGS_SANITIZEDDEBUG
      "${CMAKE_EXE_LINKER_FLAGS_SANITIZEDDEBUG} -shared-libsan -Wl,-rpath,${LLVM_RUNTIME_DIR} --rtlib=compiler-rt"
  )
  set(CMAKE_SHARED_LINKER_FLAGS_SANITIZEDDEBUG
      "${CMAKE_SHARED_LINKER_FLAGS_SANITIZEDDEBUG} -shared-libsan -Wl,-rpath,${LLVM_RUNTIME_DIR} --rtlib=compiler-rt"
  )
endif()

mark_as_advanced(
  CMAKE_CXX_FLAGS_SANITIZEDDEBUG
  CMAKE_C_FLAGS_SANITIZEDDEBUG
  CMAKE_EXE_LINKER_FLAGS_SANITIZEDDEBUG
  CMAKE_SHARED_LINKER_FLAGS_SANITIZEDDEBUG
  CMAKE_CXX_FLAGS_THREADSANITIZEDDEBUG
  CMAKE_C_FLAGS_THREADSANITIZEDDEBUG
  CMAKE_EXE_LINKER_FLAGS_THREADSANITIZEDDEBUG
  CMAKE_SHARED_LINKER_FLAGS_THREADSANITIZEDDEBUG)

list(APPEND CMAKE_CONFIGURATION_TYPES Coverage)

# Add new build types
set(CMAKE_CXX_FLAGS_COVERAGE
    "-fprofile-arcs -ftest-coverage"
    CACHE STRING "Flags used by the C++ compiler during coverage builds." FORCE)
set(CMAKE_C_FLAGS_COVERAGE
    "-fprofile-arcs -ftest-coverage"
    CACHE STRING "Flags used by the C compiler during coverage builds." FORCE)
set(CMAKE_EXE_LINKER_FLAGS_COVERAGE
    ""
    CACHE STRING "Flags used for linking binaries during coverage builds." FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_COVERAGE
    ""
    CACHE STRING "Flags used by the shared libraries linker during coverage builds." FORCE)

mark_as_advanced(CMAKE_CXX_FLAGS_COVERAGE CMAKE_C_FLAGS_COVERAGE CMAKE_EXE_LINKER_FLAGS_COVERAGE
                 CMAKE_SHARED_LINKER_FLAGS_COVERAGE)
