cmake_minimum_required(VERSION 3.15)

## Compiler definition 
# Set if not already set (no force option) : cmake -D CMAKE_C_COMPILER="/path/to/your/c/compiler/executable" will override this
# cmake -DCMAKE_CXX_COMPILER="clang-cpp-11"  -DCMAKE_C_COMPILER="clang-11" ../
# Alternatively we could use the CC and CXX env variables
# Warning : Ongoing bug where clang 11 is not able to compile
set(CMAKE_CXX_COMPILER g++ CACHE STRING "C++ compiler")
set(CMAKE_C_COMPILER gcc CACHE STRING "C compiler")

project(DDProf
  LANGUAGES CXX C
  VERSION 0.1
)

## Allow to easily import external libraries
include(FetchContent)

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

### Unit tests 
add_subdirectory(test)

### Declaration of DDProf
# Allows us to find different libraries
find_package(PkgConfig REQUIRED)

aux_source_directory(src MAIN_SRC)
aux_source_directory(src/proto PROTO_SRC)
aux_source_directory(src/eg EG_SRC)

# Define all sources
set(DDPROF_GLOBAL_SRC
   ${DDPROF_GLOBAL_SRC}
   ${MAIN_SRC}
   ${PROTO_SRC}
   ${EG_SRC})

