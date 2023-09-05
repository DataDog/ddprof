# Unless explicitly stated otherwise all files in this repository are licensed under the Apache
# License Version 2.0. This product includes software developed at Datadog
# (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

find_package(GTest REQUIRED)
find_package(benchmark REQUIRED)

add_subdirectory(no_tls)

enable_testing()

# On arm builds Leak Detection is barely usable: https://github.com/google/sanitizers/issues/703
option(DDPROF_DETECT_LEAK "Parameter for leak detection." 1)

# Code coverage
if("${CMAKE_BUILD_TYPE}" STREQUAL "Coverage")
  message(STATUS "Activating code coverage in tests")
  include(CodeCoverage)
  set(COVERAGE_EXCLUDES "vendor/*")
  setup_target_for_coverage_gcovr_xml(
    NAME ctest_coverage EXECUTABLE ctest -j ${PROCESSOR_COUNT} # Executable in PROJECT_BINARY_DIR
  )
endif()

# Define functions

#[[ Create a unit test
Syntax:
add_unit_test(<name> src1 [src2 ...] [LIBRARIES lib1 lib2 ...] [DEFINITIONS def1 def2])
will compile an unit test named <name> from source files src1 src2...
with pre-processor definitions def1 def2 (-Ddef1 -Ddef2 ... will be added to compile command)
and link against lib1 lib2 ... and libm

Examples:
add_unit_test(myexe src1.cpp)
add_unit_test(myexe src1.cpp DEFINITIONS UNIT_TEST)
#]]
function(add_unit_test name)
  set(options NO_DETECT_LEAKS)
  set(oneValueArgs)
  set(multiValueArgs)
  cmake_parse_arguments(PARSE_ARGV 1 MY "${options}" "${oneValueArgs}" "${multiValueArgs}")
  message(STATUS "Creating unit test : " ${name})

  # # Create exe with sources. Always add logger and error management in the unit tests
  add_exe(${name} ../src/ddres_list.cc ../src/logger.cc ${MY_UNPARSED_ARGUMENTS})

  target_link_libraries(${name} PRIVATE gtest Threads::Threads gmock_main gmock)
  target_include_directories(${name} PRIVATE ${DDPROF_INCLUDE_LIST} ${GTEST_INCLUDE_DIRS}
                                             ${CMAKE_SOURCE_DIR}/include/lib)

  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(
    ${name}
    PROPERTIES
      ENVIRONMENT
      "UBSAN_OPTIONS=halt_on_error=1 abort_on_error=1 print_stacktrace=1;LSAN_OPTIONS=detect_leaks=$<BOOL:${DDPROF_DETECT_LEAK}> malloc_context_size=2 print_suppressions=0;ASAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/cmake/asan.supp"
  )
  disable_clangtidy(${name})
endfunction()

#[[ Create a benchmark
Syntax:
add_benchmark(<name> src1 [src2 ...] [LIBRARIES lib1 lib2 ...] [DEFINITIONS def1 def2])
will compile an unit test named <name> from source files src1 src2...
with pre-processor definitions def1 def2 (-Ddef1 -Ddef2 ... will be added to compile command)
and link against lib1 lib2 ... and libm

Examples:
add_benchmark(myexe src1.cpp)
add_benchmark(myexe src1.cpp DEFINITIONS UNIT_TEST)
#]]
function(add_benchmark name)
  set(options)
  set(oneValueArgs)
  set(multiValueArgs)
  cmake_parse_arguments(PARSE_ARGV 1 MY "${options}" "${oneValueArgs}" "${multiValueArgs}")
  message(STATUS "Creating unit test : " ${name})

  # # Create exe with sources. Always add logger and error management in the unit tests
  add_exe(${name} ../src/ddres_list.cc ../src/logger.cc ${MY_UNPARSED_ARGUMENTS})

  target_link_libraries(${name} PRIVATE Threads::Threads benchmark::benchmark
                                        benchmark::benchmark_main)
  target_include_directories(${name} PRIVATE ${DDPROF_INCLUDE_LIST} ${CMAKE_SOURCE_DIR}/include/lib)
  disable_clangtidy(${name})
endfunction()

# Definition of unit tests
add_compile_definitions("UNIT_TEST_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")

add_unit_test(ddprofcmdline-ut ../src/ddprof_cmdline.cc ../src/perf_watcher.cc
              ../src/tracepoint_config.cc ddprofcmdline-ut.cc LIBRARIES DDProf::Parser)

add_unit_test(logger-ut logger-ut.cc)

add_unit_test(signal_helper-ut ../src/signal_helper.cc signal_helper-ut.cc)

include(Version)
add_unit_test(version-ut ../src/version.cc version-ut.cc DEFINITIONS ${DDPROF_DEFINITION_LIST})

add_unit_test(statsd-ut ../src/statsd.cc statsd-ut.cc)

add_unit_test(ddprof_stats-ut ../src/ddprof_stats.cc ../src/statsd.cc ddprof_stats-ut.cc)

add_unit_test(
  demangle-ut demangle-ut.cc ../src/demangler/demangler.cc
  LIBRARIES llvm-demangle
  DEFINITIONS MYNAME="demangle-ut")

add_unit_test(ipc-ut ../src/ipc.cc ipc-ut.cc)

add_unit_test(mmap-ut ../src/perf.cc ../src/perf_watcher.cc mmap-ut.cc DEFINITIONS MYNAME="mmap-ut")
target_include_directories(mmap-ut PRIVATE)

add_unit_test(ddres-ut ddres-ut.cc DEFINITIONS MYNAME="ddres-ut")

add_unit_test(
  cap-ut ../src/cap_display.cc cap-ut.cc
  LIBRARIES libcap
  DEFINITIONS MYNAME="cap-ut")
target_include_directories(cap-ut PRIVATE ${LIBCAP_INCLUDE_DIR})

add_unit_test(user_id-ut user_id-ut.cc ../src/user_override.cc DEFINITIONS MYNAME="userid-ut")

add_unit_test(procutils-ut ../src/procutils.cc procutils-ut.cc DEFINITIONS MYNAME="procutils-ut")

add_unit_test(
  ddprof_context-ut
  ../src/ddprof_cmdline.cc
  ../src/ddprof_cli.cc
  ../src/ddprof_context_lib.cc
  ../src/ddprof_cpumask.cc
  ../src/logger_setup.cc
  ../src/perf_watcher.cc
  ../src/presets.cc
  ../src/tracepoint_config.cc
  ddprof_context-ut.cc
  LIBRARIES DDProf::Parser CLI11
  DEFINITIONS MYNAME="ddprof_context-ut")

add_unit_test(
  perf_ringbuffer-ut ../src/perf.cc ../src/perf_watcher.cc ../src/perf_ringbuffer.cc
  ../src/tracepoint_config.cc perf_ringbuffer-ut.cc DEFINITIONS MYNAME="perf_ringbuffer-ut")

add_unit_test(
  pevent-ut
  ../src/ddprof_cmdline.cc
  ../src/tracepoint_config.cc
  ../src/pevent_lib.cc
  ../src/user_override.cc
  ../src/perf.cc
  ../src/perf_watcher.cc
  ../src/perf_ringbuffer.cc
  ../src/ringbuffer_utils.cc
  ../src/sys_utils.cc
  pevent-ut.cc
  LIBRARIES DDProf::Parser
  DEFINITIONS MYNAME="pevent-ut")

add_unit_test(
  ddprof_pprof-ut ../src/pprof/ddprof_pprof.cc ../src/perf_watcher.cc ddprof_pprof-ut.cc
  LIBRARIES Datadog::Profiling DDProf::Parser
  DEFINITIONS MYNAME="ddprof_pprof-ut")

add_unit_test(
  ddprof_exporter-ut
  ../src/exporter/ddprof_exporter.cc
  ../src/ddprof_cmdline.cc
  ../src/tracepoint_config.cc
  ../src/pprof/ddprof_pprof.cc
  ../src/perf_watcher.cc
  ../src/tags.cc
  ddprof_exporter-ut.cc
  LIBRARIES Datadog::Profiling DDProf::Parser
  DEFINITIONS MYNAME="ddprof_exporter-ut")

add_unit_test(
  dso-ut
  ../src/dso.cc
  ../src/dso_hdr.cc
  ../src/procutils.cc
  ../src/signal_helper.cc
  ../src/user_override.cc
  dso-ut.cc
  DEFINITIONS MYNAME="dso-ut")
target_include_directories(dso-ut PRIVATE ${LIBCAP_INCLUDE_DIR})

add_unit_test(tags-ut tags-ut.cc ../src/tags.cc ../src/thread_info.cc DEFINITIONS MYNAME="tags-ut")
target_include_directories(tags-ut PRIVATE ${DOGFOOD_INCLUDE_DIR})

add_unit_test(
  dwfl_symbol-ut dwfl_symbol-ut.cc ../src/dwfl_symbol.cc ../src/demangler/demangler.cc
  LIBRARIES llvm-demangle ${ELFUTILS_LIBRARIES}
  DEFINITIONS MYNAME="dwfl_symbol-ut")

add_unit_test(
  dwfl_module-ut
  dwfl_module-ut.cc
  ../src/build_id.cc
  ../src/dwfl_hdr.cc
  ../src/ddprof_module_lib.cc
  ../src/dwfl_symbol.cc
  ../src/demangler/demangler.cc
  ../src/dso.cc
  ../src/dso_hdr.cc
  ../src/failed_assumption.cc
  ../src/procutils.cc
  ../src/signal_helper.cc
  ../src/user_override.cc
  LIBRARIES ${ELFUTILS_LIBRARIES} llvm-demangle
  DEFINITIONS MYNAME="dwfl_symbol-ut")
add_compile_definitions("DWFL_TEST_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")
set_property(TARGET dwfl_module-ut PROPERTY POSITION_INDEPENDENT_CODE TRUE)

add_unit_test(
  savecontext-ut
  savecontext-ut.cc
  ../src/base_frame_symbol_lookup.cc
  ../src/build_id.cc
  ../src/common_mapinfo_lookup.cc
  ../src/common_symbol_lookup.cc
  ../src/container_id.cc
  ../src/ddprof_process.cc
  ../src/ddprof_stats.cc
  ../src/dso.cc
  ../src/dso_hdr.cc
  ../src/dso_symbol_lookup.cc
  ../src/dwfl_hdr.cc
  ../src/ddprof_module_lib.cc
  ../src/dwfl_symbol.cc
  ../src/dwfl_symbol_lookup.cc
  ../src/dwfl_thread_callbacks.cc
  ../src/demangler/demangler.cc
  ../src/jit/jitdump.cc
  ../src/failed_assumption.cc
  ../src/lib/pthread_fixes.cc
  ../src/lib/savecontext.cc
  ../src/lib/saveregisters.cc
  ../src/mapinfo_lookup.cc
  ../src/procutils.cc
  ../src/runtime_symbol_lookup.cc
  ../src/symbol_map.cc
  ../src/signal_helper.cc
  ../src/statsd.cc
  ../src/unwind.cc
  ../src/unwind_dwfl.cc
  ../src/unwind_helpers.cc
  ../src/unwind_metrics.cc
  ../src/user_override.cc
  LIBRARIES ${ELFUTILS_LIBRARIES} llvm-demangle
  DEFINITIONS MYNAME="savecontext-ut")

add_unit_test(
  allocation_tracker-ut
  allocation_tracker-ut.cc
  ../src/lib/allocation_tracker.cc
  ../src/lib/address_bitset.cc
  ../src/base_frame_symbol_lookup.cc
  ../src/build_id.cc
  ../src/container_id.cc
  ../src/common_mapinfo_lookup.cc
  ../src/common_symbol_lookup.cc
  ../src/ddprof_cmdline.cc
  ../src/ddprof_process.cc
  ../src/ddprof_stats.cc
  ../src/dso.cc
  ../src/dso_hdr.cc
  ../src/dso_symbol_lookup.cc
  ../src/dwfl_hdr.cc
  ../src/ddprof_module_lib.cc
  ../src/dwfl_symbol.cc
  ../src/dwfl_symbol_lookup.cc
  ../src/dwfl_thread_callbacks.cc
  ../src/demangler/demangler.cc
  ../src/jit/jitdump.cc
  ../src/failed_assumption.cc
  ../src/pevent_lib.cc
  ../src/perf.cc
  ../src/perf_ringbuffer.cc
  ../src/perf_watcher.cc
  ../src/ringbuffer_utils.cc
  ../src/lib/pthread_fixes.cc
  ../src/lib/savecontext.cc
  ../src/lib/saveregisters.cc
  ../src/mapinfo_lookup.cc
  ../src/procutils.cc
  ../src/runtime_symbol_lookup.cc
  ../src/symbol_map.cc
  ../src/signal_helper.cc
  ../src/statsd.cc
  ../src/sys_utils.cc
  ../src/tracepoint_config.cc
  ../src/user_override.cc
  ../src/unwind.cc
  ../src/unwind_dwfl.cc
  ../src/unwind_helpers.cc
  ../src/unwind_metrics.cc
  LIBRARIES ${ELFUTILS_LIBRARIES} llvm-demangle DDProf::Parser
  DEFINITIONS ${DDPROF_DEFINITION_LIST} KMAX_TRACKED_ALLOCATIONS=16384)

add_unit_test(sys_utils-ut sys_utils-ut.cc ../src/sys_utils.cc)

add_unit_test(
  ringbuffer-ut
  ringbuffer-ut.cc
  ../src/ddprof_cmdline.cc
  ../src/tracepoint_config.cc
  ../src/perf.cc
  ../src/perf_ringbuffer.cc
  ../src/perf_watcher.cc
  ../src/pevent_lib.cc
  ../src/ringbuffer_utils.cc
  ../src/sys_utils.cc
  ../src/user_override.cc
  LIBRARIES DDProf::Parser)

add_unit_test(timer-ut timer-ut.cc ../src/timer.cc ../src/perf.cc)

add_unit_test(ddprof_file_info-ut ddprof_file_info-ut.cc)

add_unit_test(runtime_symbol_lookup-ut runtime_symbol_lookup-ut.cc ../src/runtime_symbol_lookup.cc
              ../src/symbol_map.cc ../src/jit/jitdump.cc)

add_unit_test(ddprof_cpumask-ut ddprof_cpumask-ut.cc ../src/ddprof_cpumask.cc)

add_unit_test(symbol_map-ut symbol_map-ut.cc ../src/symbol_map.cc)

add_unit_test(build_id-ut build_id-ut.cc ../src/build_id.cc)

add_unit_test(jitdump-ut jitdump-ut.cc ../src/jit/jitdump.cc)

add_unit_test(tracepoint_config-ut tracepoint_config-ut.cc ../src/tracepoint_config.cc)

add_unit_test(live_allocation-ut live_allocation-ut.cc ../src/live_allocation.cc)

add_unit_test(ddprof_process-ut ddprof_process-ut.cc ../src/container_id.cc
              ../src/ddprof_process.cc)

add_unit_test(glibc_fixes-ut glibc_fixes-ut.cc ../src/lib/glibc_fixes.c LIBRARIES pthread)

add_unit_test(reentry_guard-ut reentry_guard-ut.cc)

add_unit_test(
  ddprof_cli-ut
  ddprof_cli-ut.cc
  ../src/ddprof_cli.cc
  ../src/version.cc
  ../src/ddprof_cmdline.cc
  ../src/perf_watcher.cc
  ../src/tracepoint_config.cc
  LIBRARIES DDProf::Parser CLI11)

add_unit_test(pthread_tls-ut pthread_tls-ut.cc)

add_unit_test(address_bitset-ut address_bitset-ut.cc ../src/lib/address_bitset.cc)

add_benchmark(savecontext-bench savecontext-bench.cc ../src/lib/pthread_fixes.cc
              ../src/lib/savecontext.cc ../src/lib/saveregisters.cc)

add_benchmark(timer-bench timer-bench.cc ../src/timer.cc ../src/perf.cc)

add_benchmark(
  allocation_tracker-bench
  allocation_tracker-bench.cc
  ../src/lib/address_bitset.cc
  ../src/lib/allocation_tracker.cc
  ../src/pevent_lib.cc
  ../src/perf.cc
  ../src/perf_ringbuffer.cc
  ../src/perf_watcher.cc
  ../src/ringbuffer_utils.cc
  ../src/lib/pthread_fixes.cc
  ../src/lib/savecontext.cc
  ../src/lib/saveregisters.cc
  ../src/procutils.cc
  ../src/user_override.cc
  ../src/sys_utils.cc
  LIBRARIES ${ELFUTILS_LIBRARIES} llvm-demangle DDProf::Parser
  DEFINITIONS ${DDPROF_DEFINITION_LIST} KMAX_TRACKED_ALLOCATIONS=16384)

if(NOT CMAKE_BUILD_TYPE STREQUAL "SanitizedDebug")
  add_exe(
    simple_malloc-static simple_malloc.cc
    LIBRARIES Threads::Threads dd_profiling-static CLI11 dl
    DEFINITIONS USE_DD_PROFILING)
  target_include_directories(simple_malloc-static PRIVATE ${CMAKE_SOURCE_DIR}/include)

  add_exe(
    simple_malloc-shared simple_malloc.cc
    LIBRARIES dd_profiling-shared Threads::Threads CLI11 dl
    DEFINITIONS USE_DD_PROFILING)
  target_include_directories(simple_malloc-shared PRIVATE ${CMAKE_SOURCE_DIR}/include)

  add_exe(simple_malloc simple_malloc.cc LIBRARIES Threads::Threads CLI11 dl)
  target_include_directories(simple_malloc PRIVATE ${CMAKE_SOURCE_DIR}/include)

  add_library(simple_malloc_lib SHARED simple_malloc.cc)
  target_compile_definitions(simple_malloc_lib PRIVATE "SIMPLE_MALLOC_SHARED_LIBRARY")
  set_target_properties(simple_malloc_lib PROPERTIES OUTPUT_NAME "simplemalloc")
  target_include_directories(simple_malloc_lib PRIVATE ${CMAKE_SOURCE_DIR}/include)

  add_test(
    NAME simple_malloc
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/simple_malloc-ut.sh
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

  add_test(
    NAME check_no_tls
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/check_no_tls-ut.sh
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

  if(BUILD_UNIVERSAL_DDPROF)
    add_test(NAME check-injected-lib
             COMMAND ${CMAKE_SOURCE_DIR}/tools/check_for_unsafe_libc_functions.py
                     $<TARGET_FILE:dd_profiling-embedded>)
  endif()
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "SanitizedDebug")
  add_exe(pthread_deadlock pthread_deadlock.cc LIBRARIES Threads::Threads)
  add_test(
    NAME pthread_deadlock
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/pthread_deadlock-ut.sh
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  add_exe(read_past_sp read_past_sp.cc)
  target_include_directories(read_past_sp PRIVATE ${DDPROF_INCLUDE_LIST})
  add_test(
    NAME read_past_sp
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/read_past_sp-ut.sh
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

add_test(
  NAME ddprof_help
  COMMAND ddprof -h
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_exe(deep_stacks deep_stacks.cc)
