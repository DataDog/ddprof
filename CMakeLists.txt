cmake_minimum_required(VERSION 3.15)
############################
### Global definitions   ###
############################

## Compiler definition 
# Set if not already set (no force option) : cmake -D CMAKE_C_COMPILER="/path/to/your/c/compiler/executable" will override this
# cmake -DCMAKE_CXX_COMPILER="clang-cpp-11"  -DCMAKE_C_COMPILER="clang-11" ../
# Alternatively you can use the CC and CXX env variables
set(CMAKE_CXX_COMPILER g++ CACHE STRING "C++ compiler")
set(CMAKE_C_COMPILER gcc CACHE STRING "C compiler")
 
project(DDProf
  LANGUAGES C
  VERSION 0.1
)
add_compile_definitions("MYNAME=\"${CMAKE_PROJECT_NAME}\"")

set(CMAKE_CONFIGURATION_TYPES "Debug;Release;SanitizedDebug" CACHE STRING
    "Available build-types: Debug, Release and SanitizedDebug")

## Frame pointers
set(FRAME_PTR_FLAG "-fno-omit-frame-pointer")

## Sanitizer setup
set(ASAN_FLAGS "-g -fsanitize=address -fsanitize=undefined -fsanitize=leak -fsanitize=float-divide-by-zero -fno-sanitize-recover")
set(STACK_FLAGS "-fstack-protector-all")
set(CMAKE_CXX_FLAGS_SANITIZEDDEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${ASAN_FLAGS} ${STACK_FLAGS}")
set(CMAKE_C_FLAGS_SANITIZEDDEBUG "${CMAKE_C_FLAGS_DEBUG} ${ASAN_FLAGS} ${STACK_FLAGS}")

## Allow to easily import external libraries
include(FetchContent)

# Check for pthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

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

####################
### Unit tests   ###
####################

### Unit tests
# Add infrastructure for enabling tests
option(BUILD_DDPROF_TESTING "Enable tests" ON)
if (${BUILD_DDPROF_TESTING})
  enable_language(CXX)
  enable_testing()
  add_subdirectory(test)
endif()

###############################
### Declaration of DDProf   ###
###############################
# Known limitation : lib elf is not built with cmake for now (makefile)

# Leave frame pointers to help with profiling
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FRAME_PTR_FLAG}")

find_package(PkgConfig REQUIRED)

set(DDPROF_INCLUDE_LIST ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include/proto)
list(APPEND DDPROF_INCLUDE_LIST ${CMAKE_SOURCE_DIR}/include/proto)

# Define the scripts to help find the libraries
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(libddprof REQUIRED)
if(NOT LIBDDPROF_INCLUDE_DIR)
  set(LIBDDPROF_INCLUDE_DIR, "${CMAKE_SOURCE_DIR}/vendor/libddprof/RelWithDebInfo/include")
endif()
message(STATUS "Libddprof Include directory " ${LIBDDPROF_INCLUDE_DIR})
list(APPEND DDPROF_INCLUDE_LIST ${LIBDDPROF_INCLUDE_DIR})

# Check that we are on a linux system
message(STATUS "SYSTEM NAME " ${CMAKE_SYSTEM_NAME})
if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
   message(STATUS "BUILD WILL FAIL : NON LINUX SYSTEMS NOT HANDLED")
endif()


# Zlib
# Slightly hacky way of finding static libraries. Unable to find static library using CMAKE_FIND_LIBRARY_SUFFIXES.
find_package(ZLIB REQUIRED)
get_filename_component(LIB_DIRNAME_ZLIB ${ZLIB_LIBRARIES} DIRECTORY)
message(STATUS "zlib found in following folder : " ${LIB_DIRNAME_ZLIB})
link_directories(${LIB_DIRNAME_ZLIB})

# Lzma
find_package(LibLZMA)
list(APPEND DDPROF_INCLUDE_LIST ${LIBLZMA_INCLUDE_DIRS})
get_filename_component(LIB_DIRNAME_LZMA ${LIBLZMA_LIBRARIES} DIRECTORY)
message(STATUS "LibLZMA found in following folder : " ${LIB_DIRNAME_LZMA})
link_directories(${LIB_DIRNAME_LZMA})

#protoc
## We use pkg-config to find protobuf-c (not the same as the standard protobuf lib which suports cmake nicely)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PROTOBUF_C REQUIRED IMPORTED_TARGET libprotobuf-c)
link_directories(${PROTOBUF_C_LIBDIR})

# llvm-demangle
#set_directory_properties(PROPERTIES EP_UPDATE_DISCONNECTED true)
set(LLVM_DEMANGLE_PATH ${CMAKE_SOURCE_DIR}/vendor/llvm CACHE STRING "Path to the llvm-demangle directory")
set(LLVM_DEMANGLE_SVNROOT https://github.com/llvm/llvm-project/trunk/llvm CACHE STRING "GitHub URL for LLVM")
FetchContent_Declare(LLVMDemangleHeaders
  SVN_REPOSITORY        ${LLVM_DEMANGLE_SVNROOT}/include/llvm/Demangle
  SOURCE_DIR            ${LLVM_DEMANGLE_PATH}/include/llvm/Demangle
)
FetchContent_Declare(LLVMDemangleSources
  SVN_REPOSITORY        ${LLVM_DEMANGLE_SVNROOT}/lib/Demangle
  SOURCE_DIR            ${LLVM_DEMANGLE_PATH}/lib/Demangle
)
if(NOT LLVMDemangleHeaders_POPULATED)
  FetchContent_Populate(LLVMDemangleHeaders)
endif()
if(NOT LLVMDemangleSources_POPULATED)
  FetchContent_Populate(LLVMDemangleSources)
  aux_source_directory(${LLVM_DEMANGLE_PATH}/lib/Demangle LLVM_DEMANGLE_SOURCES)
endif()
add_library(libllvm-demangle STATIC
  src/demangle.cpp
  ${LLVM_DEMANGLE_SOURCES}
)
target_include_directories(libllvm-demangle PUBLIC
  ${LLVM_DEMANGLE_PATH}/include
)
list(APPEND DDPROF_INCLUDE_LIST ${LLVM_DEMANGLE_PATH}/include)


## Elf utils
set(ELFUTILS_PATH ${CMAKE_SOURCE_DIR}/vendor/elfutils CACHE STRING "Path to the elfutils directory")
list(APPEND DDPROF_INCLUDE_LIST ${ELFUTILS_PATH} ${ELFUTILS_PATH}/libdwfl ${ELFUTILS_PATH}/libdw ${ELFUTILS_PATH}/libebl ${ELFUTILS_PATH}/libelf)

# Add directories to look for libraries
link_directories(${ELFUTILS_PATH}/libdwfl)
link_directories(${ELFUTILS_PATH}/libdw)
link_directories(${ELFUTILS_PATH}/libebl)
link_directories(${ELFUTILS_PATH}/libelf)

# Find the source files
aux_source_directory(src MAIN_SRC)
aux_source_directory(src/proto PROTO_SRC)

# Define all sources
set(DDPROF_GLOBAL_SRC
   ${DDPROF_GLOBAL_SRC}
   ${MAIN_SRC}
   ${PROTO_SRC})

# It is important to force most libraries as static
add_exe(ddprof
        ${DDPROF_GLOBAL_SRC}
        LIBRARIES libddprof libllvm-demangle dw.a elf.a libz.a Threads::Threads libprotobuf-c.a liblzma.a)
target_include_directories(ddprof PRIVATE ${DDPROF_INCLUDE_LIST})

install(TARGETS ddprof)
