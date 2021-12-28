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
function(use_libcxx)
  cmake_parse_arguments(USE_LIBCXX
    "STATIC"
    "TARGET"
    ""
    ${ARGN})

  if (NOT USE_LIBCXX_TARGET)
    message("You must supply a TARGET to use_libcxx")
  endif()
    
  if("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
    target_compile_options(${USE_LIBCXX_TARGET} BEFORE PUBLIC $<$<COMPILE_LANGUAGE:CXX>:--stdlib=libc++>)
    target_link_options(${USE_LIBCXX_TARGET} BEFORE PUBLIC $<$<COMPILE_LANGUAGE:CXX>:--stdlib=libc++>)

    if (USE_LIBCXX_STATIC)
      target_compile_options(${USE_LIBCXX_TARGET} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-rtlib=compiler-rt>)
      target_link_options(${USE_LIBCXX_TARGET} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-rtlib=compiler-rt>)

      set_property(TARGET ${SETSTATICCX_TARGET} PROPERTY LIBCXX_USE_COMPILER_RT "YES")
      set_property(TARGET ${SETSTATICCX_TARGET} PROPERTY LIBCXXABI_USE_COMPILER_RT "YES")
      set_property(TARGET ${SETSTATICCX_TARGET} PROPERTY LIBCXXABI_USE_LLVM_UNWINDER "YES")

      # Find and link static libraries
      set(LLVM_LIB_DIRS "/usr/lib/llvm-12/lib")
      find_library(LIBCXX_STATIC NAMES libc++.a HINTS ${LLVM_LIB_DIRS})
      find_library(LIBCXXABI_STATIC NAMES libc++abi.a HINTS ${LLVM_LIB_DIRS})
      find_library(LIBUNWIND_STATIC NAMES libunwind.a HINTS ${LLVM_LIB_DIRS})
      target_link_libraries(${USE_LIBCXX_TARGET} PUBLIC ${LIBCXX_STATIC})
      target_link_libraries(${USE_LIBCXX_TARGET} PUBLIC ${LIBCXXABI_STATIC})
      target_link_libraries(${USE_LIBCXX_TARGET} PUBLIC ${LIBUNWIND_STATIC})
      target_link_libraries(${USE_LIBCXX_TARGET} PUBLIC liblzma.a) #found in toplevel
    endif()
  endif()
  if (USE_LIBCXX_STATIC)
    target_link_options(${USE_LIBCXX_TARGET} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-static-libstdc++>)
  endif()

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
