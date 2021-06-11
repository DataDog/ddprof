cmake_minimum_required(VERSION 3.15)

##### Temporary pending top level CMakefiles #####
# Set if not already set (no force option) : cmake -D CMAKE_C_COMPILER="/path/to/your/c/compiler/executable" will override this
# Alternatively we could use the CC and CXX env variables
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_C_COMPILER gcc)

project(DDProfUnitTests
  LANGUAGES CXX C
  VERSION 0.1
)

include(FetchContent)
##### End of top level config #####

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(googletest)

enable_testing()

#### Define level of static checking ####
message(STATUS "Build type set to " ${CMAKE_BUILD_TYPE})
message(STATUS "Activate asan by default for unit tests")

set(ASAN_FLAGS "-g -fsanitize=address -fsanitize=undefined -fsanitize=leak -fsanitize=float-divide-by-zero -fno-sanitize-recover")
set(STACK_FLAGS "-fno-omit-frame-pointer -fstack-protector-all")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ASAN_FLAGS} ${STACK_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ASAN_FLAGS} ${STACK_FLAGS}")


#### Define functions ####

#[[ Create an executable
Syntax:
add_exe(<name> src1 [src2 ...] [LIBRARIES lib1 lib2 ...] [DEFINITIONS def1 def2])
will compile an executable named <name> from source files src1 src2...
with pre-processor definitions def1 def2 (-Ddef1 -Ddef2 ... will be added to compile command)
and link against lib1 lib2 ...and libm

Examples:
add_exe(myexe src1.cpp)
add_exe(myexe src1.cpp
   LIBRARIES ${CMAKE_SOURCE_DIR}/myLib
   DEFINITIONS UNIT_TEST)
#]]
function(add_exe name)
   set(cur_var "sources")
   set(exe_sources "")
   set(exe_libraries "")
   set(exe_definitions "")
   set(exe_include_dirs "")
   foreach(arg IN LISTS ARGN)
      if (arg STREQUAL "LIBRARIES")
         set(cur_var "libraries")
      elseif (arg STREQUAL "DEFINITIONS")
         set(cur_var "definitions")
      else()
         list(APPEND exe_${cur_var} ${arg})
         if (cur_var STREQUAL "sources")
            get_filename_component(src_dir ${arg} DIRECTORY)
            list(APPEND exe_include_dirs ${src_dir})
         endif()
      endif()
   endforeach()
   add_executable(${name} ${exe_sources})
   set_target_properties(${name} PROPERTIES
       COMPILE_DEFINITIONS "${exe_definitions}"
       BUILD_RPATH "${runtime_path}")
   target_link_libraries(${name} PRIVATE ${exe_libraries})
   list(REMOVE_DUPLICATES exe_include_dirs)
   target_include_directories(${name} PRIVATE ${exe_include_dirs} ${all_includes})
endfunction()


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
   target_link_libraries(${name} PRIVATE gtest gmock gmock_main)
   target_include_directories(${name} PRIVATE ../include)

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
