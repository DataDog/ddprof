find_package(GTest REQUIRED)

enable_testing()

#### Define level of static checks ####
message(STATUS "Build type set to " ${CMAKE_BUILD_TYPE})
message(STATUS "Activate asan by default for unit tests")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ASAN_FLAGS} ${STACK_FLAGS} ${FRAME_PTR_FLAG}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ASAN_FLAGS} ${STACK_FLAGS} ${FRAME_PTR_FLAG}")

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

   ## Create exe with sources 
   add_exe(${name} ${MY_UNPARSED_ARGUMENTS})
   target_link_libraries(${name} PRIVATE gtest Threads::Threads gmock_main gmock)
   target_include_directories(${name} PRIVATE ../include ${GTEST_INCLUDE_DIRS})

   add_test(NAME ${name} COMMAND ${name})
   set_tests_properties(${name} PROPERTIES
                        ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1 abort_on_error=1 print_stacktrace=1;\
                        LSAN_OPTIONS=detect_leaks=1 malloc_context_size=2 print_suppressions=0")

endfunction()

#### Definition of unit tests ####
add_unit_test(
    ddprofcmdline-ut
    ../src/ddprofcmdline.c 
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
    ../src/logger.c
    mmap-ut.cc
    LIBRARIES libddprof-c
    DEFINITIONS MYNAME="ipc-ut"
)
target_include_directories(mmap-ut PRIVATE ${LIBDDPROF_INCLUDE_DIR})

add_unit_test(
    ddres-ut
    ../src/logger.c
    ddres-ut.cc
    LIBRARIES libddprof-c
    DEFINITIONS MYNAME="ddres-ut"
)
target_include_directories(ddres-ut PRIVATE ${LIBDDPROF_INCLUDE_DIR})


add_unit_test(
    cap-ut
    ../src/cap_display.c
    ../src/logger.c
    cap-ut.cc
    LIBRARIES libddprof-c libcap
    DEFINITIONS MYNAME="cap-ut"
)
target_include_directories(cap-ut PRIVATE ${LIBDDPROF_INCLUDE_DIR} ${LIBCAP_INCLUDE_DIR})
