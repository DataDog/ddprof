# Unless explicitly stated otherwise all files in this repository are licensed under the Apache
# License Version 2.0. This product includes software developed at Datadog
# (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

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
    if(arg STREQUAL "LIBRARIES")
      set(cur_var "libraries")
    elseif(arg STREQUAL "DEFINITIONS")
      set(cur_var "definitions")
    else()
      list(APPEND exe_${cur_var} ${arg})

      if(cur_var STREQUAL "sources")
        get_filename_component(src_dir ${arg} DIRECTORY)
        list(APPEND exe_include_dirs ${src_dir})
      endif()
    endif()
  endforeach()

  add_executable(${name} ${exe_sources})
  set_target_properties(${name} PROPERTIES COMPILE_DEFINITIONS "${exe_definitions}")
  target_link_libraries(${name} PRIVATE ${exe_libraries})
  list(REMOVE_DUPLICATES exe_include_dirs)
  target_include_directories(${name} PRIVATE ${exe_include_dirs})
endfunction()

# Set a target to statically link libstdc++
function(target_static_libcxx target)
  target_link_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-static-libstdc++>)
  target_link_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-static-libgcc>)
endfunction()

# Set a target to statically link libc
function(target_static_libc target)
  target_link_options(${target} PRIVATE "-static")
endfunction()

function(target_static_sanitizer target)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_link_options(${target} PRIVATE $<$<CONFIG:SanitizedDebug>:-static-libsan>)
  else()
    target_link_options(${target} PRIVATE $<$<CONFIG:SanitizedDebug>:-static-libasan
                        -static-libubsan>)
    target_link_options(${target} PRIVATE $<$<CONFIG:ThreadSanitizedDebug>:-static-libtsan
                        -static-libubsan>)
  endif()
endfunction()

function(detect_libc output_variable)
  file(WRITE "${CMAKE_BINARY_DIR}/temp.c" "int main() {}")
  try_compile(
    COMPILE_SUCCEEDED "${CMAKE_BINARY_DIR}/compile_tests"
    "${CMAKE_BINARY_DIR}/temp.c"
    OUTPUT_VARIABLE output
    LINK_OPTIONS "-v")

  if(NOT COMPILE_SUCCEEDED OR NOT "${output}" MATCHES "\"?-dynamic-linker\"? *\"?([^ \"]+)\"?")
    message(FATAL_ERROR "Unable to determine libc")
  endif()

  if("${CMAKE_MATCH_1}" MATCHES "-musl-")
    set(${output_variable}
        "musl"
        PARENT_SCOPE)
  else()
    set(${output_variable}
        "gnu"
        PARENT_SCOPE)
  endif()
endfunction()
