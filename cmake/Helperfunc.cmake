# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

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

# Set a target to statically include libc
function(static_link_cxx)
  cmake_parse_arguments(STATIC_LINK_CXX
    "" # option
    "TARGET" # one value arg
    "" # multi value arg
    ${ARGN})
  if (NOT STATIC_LINK_CXX_TARGET)
    message("You must supply a TARGET to static_link_cxx")
  endif()
  target_link_options(${STATIC_LINK_CXX_TARGET} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-static-libstdc++>)
endfunction()

# Set a target's disposition for clang-tidy
function(enable_clangtidy)
  cmake_parse_arguments(ENABLE_CLANGTIDY
    "NOTIDY"
    "TARGET"
    ""
    ${ARGN})
    if (NOT ENABLE_CLANGTIDY_NOTIDY)
      set(CLANG_TIDY_OPTIONS "clang-tidy; -header-filter=.; -checks=*;")
    else()
      set(CLANG_TIDY_OPTIONS "")
    endif()
    set_property(TARGET ${ENABLE_CLANGTIDY_TARGET} PROPERTY CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_OPTIONS}")
    set_property(TARGET ${ENABLE_CLANGTIDY_TARGET} PROPERTY CMAKE_C_CLANG_TIDY "${CLANG_TIDY_OPTIONS}")
endfunction()
