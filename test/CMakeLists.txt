find_package(GTest REQUIRED)

enable_testing()

### Code coverage
if("${CMAKE_BUILD_TYPE}" STREQUAL "Coverage")
    message(STATUS "Activating code coverage in tests")
    include(CodeCoverage)
    set(COVERAGE_EXCLUDES "vendor/*")
    setup_target_for_coverage_gcovr_xml(
        NAME ctest_coverage
        EXECUTABLE ctest -j ${PROCESSOR_COUNT} # Executable in PROJECT_BINARY_DIR
    )
endif()

#### Define functions ####

#[[ Create a unit test
Syntax:
add_unit_test(<name> src1 [src2 ...] [LIBRARIES lib1 lib2 ...] [DEFINITIONS def1 def2] [NO_DETECT_LEAKS])
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

   ## Create exe with sources. Always add logger and error management in the unit tests
   add_exe(${name} 
        ../src/ddres_list.c
        ../src/logger.c
        ${MY_UNPARSED_ARGUMENTS})
   add_dependencies(${name} deps)
   set_property(TARGET ${name} PROPERTY CXX_STANDARD 11)

   target_link_libraries(${name} PRIVATE gtest Threads::Threads gmock_main gmock)
   # libddprof almost always required (unlikely file from libddprof)
   target_include_directories(${name} PRIVATE ../include ${GTEST_INCLUDE_DIRS} ${LIBDDPROF_INCLUDE_DIR})

   add_test(NAME ${name} COMMAND ${name})
   set_tests_properties(${name} PROPERTIES
                        ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1 abort_on_error=1 print_stacktrace=1;\
                        LSAN_OPTIONS=detect_leaks=1 malloc_context_size=2 print_suppressions=0")

endfunction()

#### Definition of unit tests ####
add_unit_test(
    ddprofcmdline-ut
    ../src/ddprof_cmdline.c 
    ../src/perf_option.c
    ddprofcmdline-ut.cc
)

add_unit_test(
    signal_helper-ut
    ../src/signal_helper.c 
    signal_helper-ut.cc
)

include(Version)
add_unit_test(
    version-ut
    ../src/version.c 
    version-ut.cc
    DEFINITIONS ${DDPROF_DEFINITION_LIST}
)

add_unit_test(
    statsd-ut
    ../src/statsd.c 
    statsd-ut.cc
)

add_unit_test(
    ddprof_stats-ut
    ../src/ddprof_stats.c
    ../src/statsd.c 
    ddprof_stats-ut.cc
)

add_unit_test(
    demangle-ut
    demangle-ut.cc
    LIBRARIES llvm-demangle
)
target_include_directories(demangle-ut PRIVATE ${LLVM_DEMANGLE_PATH}/include)

add_compile_definitions("IPC_TEST_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")

add_unit_test(
    ipc-ut
    ../src/ipc.c
    ipc-ut.cc
)

add_unit_test(
    mmap-ut
    ../src/perf.c
    ../src/perf_option.c
    mmap-ut.cc
    DEFINITIONS MYNAME="mmap-ut"
)
target_include_directories(mmap-ut PRIVATE)

add_unit_test(
    ddres-ut
    ddres-ut.cc
    DEFINITIONS MYNAME="ddres-ut"
)

add_unit_test(
    cap-ut
    ../src/cap_display.c
    cap-ut.cc
    LIBRARIES libcap
    DEFINITIONS MYNAME="cap-ut"
)
target_include_directories(cap-ut PRIVATE ${LIBCAP_INCLUDE_DIR})

add_unit_test(
    user_id-ut
    user_id-ut.cc
    ../src/user_override.c
    DEFINITIONS MYNAME="userid-ut"
)

add_unit_test(
    procutils-ut
    ../src/procutils.c
    procutils-ut.cc
    DEFINITIONS MYNAME="procutils-ut"
)

add_unit_test(
    ddprof_input-ut
    ../src/ddprof_cmdline.c 
    ../src/ddprof_input.c
    ../src/perf_option.c
    ddprof_input-ut.cc
    DEFINITIONS MYNAME="ddprof_input-ut"
)

add_unit_test(
    perf_ringbuffer-ut
    ../src/perf.c
    ../src/perf_ringbuffer.c
    perf_ringbuffer-ut.cc
    DEFINITIONS MYNAME="perf_ringbuffer-ut"
)

add_unit_test(
    pevent-ut
    ../src/pevent_lib.c
    ../src/user_override.c
    ../src/perf.c
    ../src/perf_option.c
    ../src/perf_ringbuffer.c
    pevent-ut.cc
    DEFINITIONS MYNAME="pevent-ut"
)

add_unit_test(
    ddprof_pprof-ut
    ../src/pprof/ddprof_pprof.cc
    ../src/unwind_output.c
    ../src/perf_option.c
    ddprof_pprof-ut.cc
    LIBRARIES ddprof-ffi ${CMAKE_DL_LIBS}
    DEFINITIONS MYNAME="ddprof_pprof-ut"
)

add_unit_test(
    ddprof_exporter-ut
    ../src/exporter/ddprof_exporter.cc
    ../src/pprof/ddprof_pprof.cc
    ../src/unwind_output.c
    ../src/perf_option.c
    ../src/tags.cc
    ddprof_exporter-ut.cc
    LIBRARIES ddprof-ffi ${CMAKE_DL_LIBS}
    DEFINITIONS MYNAME="ddprof_exporter-ut"
)
target_include_directories(ddprof_exporter-ut PRIVATE ${DOGFOOD_INCLUDE_DIR})

add_unit_test(
    region_holder-ut
    ../src/region_holder.cc
    region_holder-ut.cc
    DEFINITIONS MYNAME="region_holder-ut")

add_unit_test(
    dso-ut
    ../src/dso.cc 
    ../src/dso_hdr.cc 
    ../src/signal_helper.c
    ../src/region_holder.cc
    dso-ut.cc 
    DEFINITIONS MYNAME="dso-ut")
target_include_directories(dso-ut PRIVATE ${LIBCAP_INCLUDE_DIR})


add_unit_test(
    tags-ut
    tags-ut.cc
    ../src/tags.cc
    ../src/thread_info.cc
    DEFINITIONS MYNAME="tags-ut"
)
target_include_directories(tags-ut PRIVATE ${DOGFOOD_INCLUDE_DIR})

add_unit_test(
    dwfl_symbol-ut
    dwfl_symbol-ut.cc
    ../src/dwfl_symbol.cc
    LIBRARIES llvm-demangle ${ELFUTILS_LIBRARIES}
    DEFINITIONS MYNAME="dwfl_symbol-ut"
)
target_include_directories(dwfl_symbol-ut PRIVATE ${ELFUTILS_INCLUDE_LIST} ${LLVM_DEMANGLE_PATH}/include)
