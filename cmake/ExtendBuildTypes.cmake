SET(GCC_DEBUG_FLAGS "-g -Wall")
set(ASAN_FLAGS "-g -fsanitize=address -fsanitize=undefined -fsanitize=leak -fsanitize=float-divide-by-zero -fno-sanitize-recover")
set(STACK_FLAGS "-fstack-protector-all")
## Frame pointers
set(FRAME_PTR_FLAG "-fno-omit-frame-pointer")

list(APPEND CMAKE_CONFIGURATION_TYPES SanitizedDebug)

# Add new build types
message(STATUS "Adding build types...")

SET(CMAKE_CXX_FLAGS_SANITIZEDDEBUG
    "${GCC_DEBUG_FLAGS} ${ASAN_FLAGS} ${STACK_FLAGS}"
    CACHE STRING "Flags used by the C++ compiler during sanitized builds."
    FORCE )
SET(CMAKE_C_FLAGS_SANITIZEDDEBUG
    "${GCC_DEBUG_FLAGS} ${ASAN_FLAGS} ${STACK_FLAGS}"
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
MARK_AS_ADVANCED(
    CMAKE_CXX_FLAGS_SANITIZEDDEBUG
    CMAKE_C_FLAGS_SANITIZEDDEBUG
    CMAKE_EXE_LINKER_FLAGS_SANITIZEDDEBUG
    CMAKE_SHARED_LINKER_FLAGS_SANITIZEDDEBUG )


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
