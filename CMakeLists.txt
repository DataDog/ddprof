# Unless explicitly stated otherwise all files in this repository are licensed under the Apache
# License Version 2.0. This product includes software developed at Datadog
# (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

cmake_minimum_required(VERSION 3.19)

# ---- Global definitions  ----

project(
  DDProf
  LANGUAGES C CXX
  VERSION 0.15.3
  DESCRIPTION "Datadog's native profiler for Linux")

message(STATUS "Compiler ID: ${CMAKE_C_COMPILER_ID}")
message(STATUS "System processor (platform): ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "SYSTEM NAME " ${CMAKE_SYSTEM_NAME})
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "Non Linux systems are not handled in ddprof")
endif()

include(ExternalProject)
find_program(CARGO_EXISTS cargo)
if(CARGO_EXISTS)
  ExternalProject_Add(blazesym
          PREFIX blazesym
          GIT_REPOSITORY https://github.com/libbpf/blazesym.git
          GIT_TAG v0.2.0-alpha.9
          SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blazesym
          CONFIGURE_COMMAND ""
          # todo switch betwee release / not release
          BUILD_COMMAND cargo build --release && cd capi && cargo build --release
          BUILD_IN_SOURCE TRUE
          INSTALL_COMMAND ""
          STEP_TARGETS build)

  SET(BLAZE_INCLUDE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blazesym/capi/include/)
  # To switch to static we need to push it through libdatadog
  SET(BLAZE_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blazesym/target/release/libblazesym_c.so)
endif()

# Debug command to get gcc command lines
# ~~~
# set(CMAKE_VERBOSE_MAKEFILE on)

# Add build ID info on alpine
add_link_options("LINKER:--build-id=sha1")

# Define the include path of cmake scripts
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/util")

include(ExtendBuildTypes)

# Allow cppcheck to build off of build commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default value
message(STATUS "Build type set to " ${CMAKE_BUILD_TYPE})

# Check for pthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# helper functions (defines add_exe)
include(Helperfunc)

detect_libc(LIBC_TYPE)

if(LIBC_TYPE STREQUAL "musl")
  add_compile_definitions("MUSL_LIBC")
  option(BUILD_UNIVERSAL_DDPROF "Build a universal (musl/glibc) version" ON)
  option(USE_LOADER "Use an intermediate loader for libdd_profiling" ON)
  option(USE_AUXILIARY "Use DT_AUXILIARY for libdd_profiling" OFF)
else()
  option(BUILD_UNIVERSAL_DDPROF "Build a universal (musl/glibc) version" OFF)
  option(USE_LOADER "Use an intermediate loader for libdd_profiling" OFF)
  option(USE_AUXILIARY "Use DT_AUXILIARY for libdd_profiling" OFF)
endif()

if(USE_LOADER AND USE_AUXILIARY)
  message(FATAL_ERROR "USE_LOADER and USE_AUXILIARY are incompatible")
endif()

# path to external dependencies
set(VENDOR_EXTENSION
    ""
    CACHE STRING "Extension to allow builds with different libc")
set(VENDOR_PATH
    "${CMAKE_SOURCE_DIR}/vendor${VENDOR_EXTENSION}"
    CACHE FILEPATH "Path to the vendor directory")
message(STATUS "Vendor path set to " ${VENDOR_PATH})

include(CheckIPOSupported)
check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT error)
if(LTO_SUPPORTED)
  message(STATUS "IPO / LTO supported")
else()
  message(STATUS "IPO / LTO not supported: <${error}>")
endif()

# ---- Dependencies ----

find_package(absl REQUIRED)

# libdatadog_profiling
include(Findlibdatadog)

# Event Parser
add_subdirectory(src/event_parser)

# elfutils
include(Findelfutils)

# ---- Static analysis ----
include(ClangTidy)
include(Format)

# Generated code needs to be available for cppcheck to work
include(CppcheckConfig)
add_dependencies(cppcheck DDProf::Parser)

# ---- Libraries (needed by ut) ----

option(DDPROF_ALLOCATOR "Define the type of allocator (STANDARD / JEMALLOC)" STANDARD)
if("${DDPROF_ALLOCATOR}" STREQUAL "JEMALLOC")
  # jemalloc for debug
  find_package(jemalloc REQUIRED)
  message(STATUS "Using Allocator jemalloc from:" ${JEMALLOC_LIBRARIES})
endif()

# Install lib cap to retrieve capabilities
include(Findlibcap)

# Third parties
add_subdirectory(third_party)

# ---- Benchmarks ----
option(BUILD_BENCHMARKS "Enable benchmarks" OFF)
if(${BUILD_BENCHMARKS})
  add_subdirectory(bench/collatz)
endif()

# ---- Declaration of DDProf ----
# Compile time definitions
string(TOLOWER ${CMAKE_PROJECT_NAME} CMAKE_PROJECT_NAME_LC)
list(APPEND DDPROF_DEFINITION_LIST "MYNAME=\"${CMAKE_PROJECT_NAME_LC}\"")

if("${CMAKE_BUILD_TYPE}" STREQUAL "Release" OR "${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
  list(APPEND DDPROF_DEFINITION_LIST "DDPROF_OPTIM=1")
endif()

include(Version)

# Leave frame pointers to help with profiling
string(APPEND CMAKE_C_FLAGS " ${FRAME_PTR_FLAG}")
string(APPEND CMAKE_CXX_FLAGS " ${FRAME_PTR_FLAG}")

list(APPEND DDPROF_INCLUDE_LIST ${CMAKE_SOURCE_DIR}/include ${BLAZE_INCLUDE_DIRECTORY})

# Find the source files
aux_source_directory(src COMMON_SRC)
aux_source_directory(src/pprof PPROF_SRC)
aux_source_directory(src/demangler DEMANGLER_SRC)
aux_source_directory(src/exporter EXPORTER_SRC)
aux_source_directory(src/exe EXE_SRC)
aux_source_directory(src/jit JIT_SRC)

# Define all sources
set(DDPROF_GLOBAL_SRC ${COMMON_SRC} ${DEMANGLER_SRC} ${PPROF_SRC} ${EXPORTER_SRC} ${EXE_SRC}
                      ${JIT_SRC})

set(DDPROF_LIBRARY_LIST DDProf::Parser llvm-demangle ${ELFUTILS_LIBRARIES} Threads::Threads ${BLAZE_LIBRARY})

if(ON)
  # Add the rust library - Refactoring ongoing. OFF for now
  list(PREPEND DDPROF_LIBRARY_LIST Datadog::Profiling)
endif()

if("${DDPROF_ALLOCATOR}" STREQUAL "JEMALLOC")
  list(PREPEND DDPROF_LIBRARY_LIST jemalloc::jemalloc_static)
endif()

# libcap, can be removed from version distributed to client
list(APPEND DDPROF_LIBRARY_LIST libcap)
list(APPEND DDPROF_INCLUDE_LIST ${LIBCAP_INCLUDE_DIR})

set(dd_profiling_linker_script "${CMAKE_SOURCE_DIR}/cmake/dd_profiling.version")

if(USE_LOADER)
  # Build small loader lib that is in charge of dlopening libdd_profiling-embedded.so
  add_library(dd_loader SHARED src/lib/glibc_fixes.c src/lib/lib_embedded_data.c src/lib/loader.c)
  target_include_directories(dd_loader PRIVATE ${CMAKE_SOURCE_DIR}/include
                                               ${CMAKE_SOURCE_DIR}/include/lib)
  set_target_properties(dd_loader PROPERTIES LINK_DEPENDS "${dd_profiling_linker_script}")
  target_link_options(dd_loader PRIVATE "LINKER:--version-script=${dd_profiling_linker_script}")
  target_static_libcxx(dd_loader)
  target_static_sanitizer(dd_loader)

  if(BUILD_UNIVERSAL_DDPROF)
    target_link_options(dd_loader PRIVATE "-nolibc")
  endif()

  set(LIBDD_LOADER_OBJECT "${CMAKE_BINARY_DIR}/libdd_loader.o")
  set(LIBDD_LOADER_HASH_HEADER "${CMAKE_BINARY_DIR}/libdd_loader_hash.h")
  add_custom_command(
    OUTPUT ${LIBDD_LOADER_OBJECT} ${LIBDD_LOADER_HASH_HEADER}
    # taken from https://dvdhrm.wordpress.com/2013/03/08/linking-binary-data/
    COMMAND ld -r -o ${LIBDD_LOADER_OBJECT} -z noexecstack --format=binary
            $<TARGET_FILE_NAME:dd_loader>
    COMMAND objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents
            ${LIBDD_LOADER_OBJECT}
    COMMAND ${CMAKE_SOURCE_DIR}/tools/generate_hash_header.sh ${LIBDD_LOADER_OBJECT}
            libdd_loader_hash ${LIBDD_LOADER_HASH_HEADER}
    DEPENDS dd_loader)

  add_custom_target(generate_libdd_loader_object DEPENDS ${LIBDD_LOADER_OBJECT}
                                                         ${LIBDD_LOADER_HASH_HEADER})
  add_library(libdd_loader_object OBJECT IMPORTED GLOBAL)
  set_target_properties(
    libdd_loader_object PROPERTIES IMPORTED_OBJECTS "${LIBDD_LOADER_OBJECT}"
                                   INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}")
  add_dependencies(libdd_loader_object generate_libdd_loader_object)
endif()

set(DD_PROFILING_SOURCES
    # cmake-format: sortable
    src/daemonize.cc
    src/ddprof_cmdline.cc
    src/ddres_list.cc
    src/ipc.cc
    src/lib/address_bitset.cc
    src/lib/allocation_tracker.cc
    src/lib/dd_profiling.cc
    src/lib/elfutils.cc
    src/lib/lib_embedded_data.c
    src/lib/pthread_fixes.cc
    src/lib/savecontext.cc
    src/lib/saveregisters.cc
    src/lib/symbol_overrides.cc
    src/logger.cc
    src/perf.cc
    src/perf_clock.cc
    src/perf_ringbuffer.cc
    src/perf_watcher.cc
    src/pevent_lib.cc
    src/ratelimiter.cc
    src/ringbuffer_utils.cc
    src/signal_helper.cc
    src/sys_utils.cc
    src/tracepoint_config.cc
    src/tsc_clock.cc
    src/user_override.cc)

if(BUILD_UNIVERSAL_DDPROF)
  # Compiling on different libc, we need to ensure some symbols are available everywhere
  list(APPEND DD_PROFILING_SOURCES src/lib/glibc_fixes.c src/lib/libc_compatibility.c)
endif()

# libddprofiling_embeded.so is the actual profiling library
add_library(dd_profiling-embedded SHARED ${DD_PROFILING_SOURCES})
target_include_directories(dd_profiling-embedded PUBLIC ${CMAKE_SOURCE_DIR}/include/lib
                                                        ${CMAKE_SOURCE_DIR}/include)
set_target_properties(dd_profiling-embedded
                      PROPERTIES PUBLIC_HEADER "${CMAKE_SOURCE_DIR}/include/lib/dd_profiling.h")

# Link libstdc++/libgcc statically and export only profiler API
target_static_libcxx(dd_profiling-embedded)
target_static_sanitizer(dd_profiling-embedded)
set_target_properties(dd_profiling-embedded PROPERTIES LINK_DEPENDS "${dd_profiling_linker_script}")
target_link_options(dd_profiling-embedded PRIVATE
                    "LINKER:--version-script=${dd_profiling_linker_script};LINKER:-Bsymbolic")
if(BUILD_UNIVERSAL_DDPROF)
  target_link_options(dd_profiling-embedded PRIVATE "-nolibc")
  if(USE_AUXILIARY)
    target_link_options(
      dd_profiling-embedded PRIVATE
      "-Wl,-f,libpthread.so.0;-Wl,-f,libm.so.6;-Wl,-f,libdl.so.2;-Wl,-f,librt.so.1")
    target_compile_definitions(dd_profiling-embedded PRIVATE DDPROF_MISSING_FSTAT)
  endif()
endif()

# Fix for link error in sanitizeddebug build mode with gcc:
# ~~~
# /usr/bin/ld: ./libdd_profiling.so: undefined reference to `__dynamic_cast'
# /usr/bin/ld: ./libdd_profiling.so: undefined reference to `typeinfo for __cxxabiv1::__vmi_class_type_info'
# ~~~
# The cause of the error is that gcc puts `-lstdc++` before `-lubsan` in the linker invocation.
# Workaround is to add another `-lstdc++` after `-lubsan` at the end, we cannot use
# `-static-libstdc++` because it does not force gcc to add another `-lstdc++` at the end.
target_link_libraries(
  dd_profiling-embedded
  PRIVATE
    "$<$<AND:$<C_COMPILER_ID:GNU>,$<CONFIG:SanitizedDebug>>:-Wl,-Bstatic;-lubsan;-lasan;-lstdc++;-Wl,-Bdynamic>"
)
target_link_libraries(dd_profiling-embedded PUBLIC dl pthread rt absl::base absl::str_format)

set(LIBDD_PROFILING_EMBEDDED_OBJECT "${CMAKE_BINARY_DIR}/libdd_profiling-embedded.o")
set(LIBDD_PROFILING_EMBEDDED_HASH_HEADER "${CMAKE_BINARY_DIR}/libdd_profiling-embedded_hash.h")
add_custom_command(
  OUTPUT ${LIBDD_PROFILING_EMBEDDED_OBJECT} ${LIBDD_PROFILING_EMBEDDED_HASH_HEADER}
  # taken from https://dvdhrm.wordpress.com/2013/03/08/linking-binary-data/
  COMMAND ld -r -o ${LIBDD_PROFILING_EMBEDDED_OBJECT} -z noexecstack --format=binary
          $<TARGET_FILE_NAME:dd_profiling-embedded>
  COMMAND objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents
          ${LIBDD_PROFILING_EMBEDDED_OBJECT}
  COMMAND ${CMAKE_SOURCE_DIR}/tools/generate_hash_header.sh ${LIBDD_PROFILING_EMBEDDED_OBJECT}
          libdd_profiling_embedded_hash ${LIBDD_PROFILING_EMBEDDED_HASH_HEADER}
  DEPENDS dd_profiling-embedded)

add_custom_target(
  generate_libddprofiling_embedded_object DEPENDS ${LIBDD_PROFILING_EMBEDDED_OBJECT}
                                                  ${LIBDD_PROFILING_EMBEDDED_HASH_HEADER})
add_library(libddprofiling_embedded_object OBJECT IMPORTED GLOBAL)
set_target_properties(
  libddprofiling_embedded_object PROPERTIES IMPORTED_OBJECTS "${LIBDD_PROFILING_EMBEDDED_OBJECT}"
                                            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}")
add_dependencies(libddprofiling_embedded_object generate_libddprofiling_embedded_object)

# It is important to force most libraries as static
add_exe(
  ddprof ${DDPROF_GLOBAL_SRC}
  LIBRARIES ${DDPROF_LIBRARY_LIST}
  DEFINITIONS ${DDPROF_DEFINITION_LIST})
target_link_libraries(ddprof PRIVATE libddprofiling_embedded_object CLI11 absl::base
                                     absl::str_format)
if(USE_LOADER)
  target_compile_definitions(ddprof PRIVATE "DDPROF_USE_LOADER")
  target_link_libraries(ddprof PRIVATE libdd_loader_object)
endif()

target_include_directories(ddprof PRIVATE ${DDPROF_INCLUDE_LIST})
target_static_libcxx(ddprof)
target_static_sanitizer(ddprof)
if(BUILD_UNIVERSAL_DDPROF)
  target_static_libc(ddprof)
endif()

# Link time optim
if(LTO_SUPPORTED AND "${CMAKE_BUILD_TYPE}" STREQUAL "Release")
  set_property(TARGET ddprof PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

message(STATUS "Install destination " ${CMAKE_INSTALL_PREFIX})
install(FILES LICENSE LICENSE-3rdparty.csv LICENSE.LGPLV3 NOTICE DESTINATION ddprof)

set(DDPROF_EXE_OBJECT "${CMAKE_BINARY_DIR}/ddprof.o")
set(DDPROF_EXE_HASH_HEADER "${CMAKE_BINARY_DIR}/ddprof_exe_hash.h")
add_custom_command(
  OUTPUT ${DDPROF_EXE_OBJECT} ${DDPROF_EXE_HASH_HEADER}
  # taken from https://dvdhrm.wordpress.com/2013/03/08/linking-binary-data/
  COMMAND ld -r -o ${DDPROF_EXE_OBJECT} -z noexecstack --format=binary $<TARGET_FILE_NAME:ddprof>
  COMMAND objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents
          ${DDPROF_EXE_OBJECT}
  COMMAND ${CMAKE_SOURCE_DIR}/tools/generate_hash_header.sh ${DDPROF_EXE_OBJECT} ddprof_exe_hash
          ${DDPROF_EXE_HASH_HEADER}
  DEPENDS ddprof)

# add_custom_target is required here, because multiple targets depend on ${DDPROF_EXE_OBJECT}
# Without add_custom_target (only add_custom_command), each dependent target will run the custom
# command and they will race, leading to spurious failures.
add_custom_target(generate_ddprof_object DEPENDS ${DDPROF_EXE_OBJECT})

add_library(ddprof_exe_object OBJECT IMPORTED GLOBAL)
set_target_properties(
  ddprof_exe_object PROPERTIES IMPORTED_OBJECTS "${DDPROF_EXE_OBJECT}" INTERFACE_INCLUDE_DIRECTORIES
                                                                       "${CMAKE_BINARY_DIR}")
add_dependencies(ddprof_exe_object generate_ddprof_object)

add_library(dd_profiling-static STATIC ${DD_PROFILING_SOURCES})
set_target_properties(dd_profiling-static PROPERTIES OUTPUT_NAME dd_profiling)
target_compile_definitions(dd_profiling-static PRIVATE DDPROF_EMBEDDED_EXE_DATA)
target_include_directories(dd_profiling-static PUBLIC ${CMAKE_SOURCE_DIR}/include/lib
                                                      ${CMAKE_SOURCE_DIR}/include)
set_target_properties(dd_profiling-static
                      PROPERTIES PUBLIC_HEADER "${CMAKE_SOURCE_DIR}/include/lib/dd_profiling.h")
target_link_libraries(dd_profiling-static PRIVATE ddprof_exe_object)
target_link_libraries(dd_profiling-static PUBLIC dl pthread rt absl::base absl::str_format)

if(USE_LOADER)
  # When using a loader, libdd_profiling.so is a loader that embeds both libdd_profiling-embedded.so
  # and ddprof executable.
  add_library(dd_profiling-shared SHARED src/lib/glibc_fixes.c src/lib/lib_embedded_data.c
                                         src/lib/loader.c)
  target_link_libraries(dd_profiling-shared PRIVATE libddprofiling_embedded_object
                                                    ddprof_exe_object)
  target_compile_definitions(dd_profiling-shared PRIVATE DDPROF_EMBEDDED_LIB_DATA
                                                         DDPROF_EMBEDDED_EXE_DATA)
else()
  # Without loader, libdd_profiling.so is basically the same as libdd_profiling-embedded.so plus an
  # embedded ddprof executable.
  add_library(dd_profiling-shared SHARED ${DD_PROFILING_SOURCES} src/lib/lib_embedded_data.c)
  target_link_libraries(dd_profiling-shared PRIVATE ddprof_exe_object)
  target_compile_definitions(dd_profiling-shared PRIVATE DDPROF_EMBEDDED_EXE_DATA)

  # Fix for link error in sanitizeddebug build mode with gcc:
  # ~~~
  # /usr/bin/ld: ./libdd_profiling.so: undefined reference to `__dynamic_cast'
  # /usr/bin/ld: ./libdd_profiling.so: undefined reference to `typeinfo for __cxxabiv1::__vmi_class_type_info'
  # ~~~
  # The cause of the error is that gcc puts `-lstdc++` before `-lubsan` in the linker invocation.
  # Workaround is to add another `-lstdc++` after `-lubsan` at the end, we cannot use
  # `-static-libstdc++` because it does not force gcc to add another `-lstdc++` at the end.
  target_link_libraries(
    dd_profiling-shared
    PRIVATE
      "$<$<AND:$<C_COMPILER_ID:GNU>,$<CONFIG:SanitizedDebug>>:-Wl,-Bstatic;-lubsan;-lasan;-lstdc++;-Wl,-Bdynamic>"
  )
  target_link_libraries(dd_profiling-shared PUBLIC dl pthread rt absl::base absl::str_format)
endif()

target_static_libcxx(dd_profiling-shared)
target_static_sanitizer(dd_profiling-shared)
set_target_properties(dd_profiling-shared PROPERTIES LINK_DEPENDS "${dd_profiling_linker_script}")
target_link_options(dd_profiling-shared PRIVATE
                    "LINKER:--version-script=${dd_profiling_linker_script};LINKER:-Bsymbolic")

set_target_properties(dd_profiling-shared PROPERTIES OUTPUT_NAME dd_profiling)
target_include_directories(dd_profiling-shared PUBLIC ${CMAKE_SOURCE_DIR}/include/lib
                                                      ${CMAKE_SOURCE_DIR}/include)
set_target_properties(dd_profiling-shared
                      PROPERTIES PUBLIC_HEADER "${CMAKE_SOURCE_DIR}/include/lib/dd_profiling.h")
if(BUILD_UNIVERSAL_DDPROF)
  target_link_options(dd_profiling-shared PRIVATE "-nolibc")
  if(USE_AUXILIARY)
    target_link_options(
      dd_profiling-shared PRIVATE
      "-Wl,-f,libpthread.so.0;-Wl,-f,libm.so.6;-Wl,-f,libdl.so.2;-Wl,-f,librt.so.1")
  endif()
endif()

install(
  TARGETS ddprof dd_profiling-static dd_profiling-shared
  RUNTIME DESTINATION ddprof/bin
  LIBRARY DESTINATION ddprof/lib
  ARCHIVE DESTINATION ddprof/lib
  PUBLIC_HEADER DESTINATION ddprof/include)

# ---- Unit tests ----

# Unit tests Add infrastructure for enabling tests
option(BUILD_DDPROF_TESTING "Enable tests" ON)
if(${BUILD_DDPROF_TESTING})
  enable_testing()
  add_subdirectory(test)
endif()
