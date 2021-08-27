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
   add_dependencies(${name} ddprof-deps)
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
    demangle-ut
    ../src/demangle.cpp
    demangle-ut.cc
    LIBRARIES llvm-demangle libstdc++.a
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
    LIBRARIES ddprof-c
    DEFINITIONS MYNAME="mmap-ut"
)
target_include_directories(mmap-ut PRIVATE)

add_unit_test(
    ddres-ut
    ddres-ut.cc
    LIBRARIES ddprof-c
    DEFINITIONS MYNAME="ddres-ut"
)

add_unit_test(
    cap-ut
    ../src/cap_display.c
    cap-ut.cc
    LIBRARIES ddprof-c libcap
    DEFINITIONS MYNAME="cap-ut"
)
target_include_directories(cap-ut PRIVATE ${LIBCAP_INCLUDE_DIR})

add_unit_test(
    user_id-ut
    user_id-ut.cc
    ../src/user_override.c
    LIBRARIES ddprof-c
    DEFINITIONS MYNAME="userid-ut"
)

add_unit_test(
    procutils-ut
    ../src/procutils.c
    procutils-ut.cc
    LIBRARIES ddprof-c
    DEFINITIONS MYNAME="procutils-ut"
)
target_include_directories(cap-ut PRIVATE ${LIBCAP_INCLUDE_DIR})

add_unit_test(
    pevent-ut
    ../src/pevent_lib.c
    ../src/user_override.c
    ../src/perf.c
    ../src/perf_option.c
    pevent-ut.cc
    LIBRARIES ddprof-c
    DEFINITIONS MYNAME="pevent-ut"
)
target_include_directories(pevent-ut PRIVATE ${LIBCAP_INCLUDE_DIR})
