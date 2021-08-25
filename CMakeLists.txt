cmake_minimum_required(VERSION 3.19)

############################
### Global definitions   ###
############################

 project(DDProf
  LANGUAGES C CXX
  VERSION 0.5.3
  DESCRIPTION "Datadog's native profiler"
)

message(STATUS "Compiler ID : ${CMAKE_C_COMPILER_ID}")

# Debug command to get gcc command lines
# set( CMAKE_VERBOSE_MAKEFILE on )

# Define the include path of cmake scripts
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(ExtendBuildTypes)

# Default value
option(CMAKE_BUILD_TYPE "Build type" Debug)
message(STATUS "Build type set to " ${CMAKE_BUILD_TYPE})

## Allow to easily import external libraries
include(FetchContent)

# Check for pthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

#helper functions (defines add_exe)
include(Helperfunc)

# path to external dependencies
set(VENDOR_PATH ${CMAKE_SOURCE_DIR}/vendor CACHE FILEPATH " Path to the vendor directory")


##############
### Deps   ###
##############
# Define a dependency target to fetch all dependencies ahead of time (CI build time optim)

# libddprof
include(Findlibddprof)
message(STATUS "Libddprof Include directory " ${LIBDDPROF_INCLUDE_DIR})

include(Findelfutils)

# llvm-demangle
include(FindLibLlvmDemangle)

add_custom_target(deps
                  DEPENDS llvm-deps ddprof-deps elfutils-deps)

#######################
### Static analysis ###
#######################

#Cpp Check
include(Cppcheckconfig)
include(Format)

##################################
### Libraries (needed by ut)   ###
##################################
find_package(PkgConfig REQUIRED)

option(JEMALLOC "Enable jemalloc stats" OFF)
if (${JEMALLOC})
   #jemalloc for debug
   include(Jemalloc)
   message(STATUS "Adding jemalloc for DBG purpose" ${JEMALLOC_ROOT_DIR})
   add_compile_definitions("DBG_JEMALLOC")
endif()

# Install lib cap to retrieve capabilities
include(Findlibcap)

####################
### Unit tests   ###
####################

### Unit tests
# Add infrastructure for enabling tests
option(BUILD_DDPROF_TESTING "Enable tests" ON)
if (${BUILD_DDPROF_TESTING})
  enable_testing()
  add_subdirectory(test)
endif()

###################
### Benchmarks  ###
###################
option(BUILD_BENCHMARKS "Enable tests" OFF)
if (${BUILD_BENCHMARKS})
  add_subdirectory(bench/collatz)
endif()

###############################
### Declaration of DDProf   ###
###############################
# Compile time definitions 
string(TOLOWER ${CMAKE_PROJECT_NAME} CMAKE_PROJECT_NAME_LC)
list(APPEND DDPROF_DEFINITION_LIST "MYNAME=\"${CMAKE_PROJECT_NAME_LC}\"")

include(Version)

# Leave frame pointers to help with profiling
list(APPEND CMAKE_C_FLAGS ${FRAME_PTR_FLAG})
list(APPEND CMAKE_CXX_FLAGS ${FRAME_PTR_FLAG})

set(DDPROF_INCLUDE_LIST ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include/proto)
list(APPEND DDPROF_INCLUDE_LIST ${CMAKE_SOURCE_DIR}/include/proto)

#libddprof
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

list(APPEND DDPROF_INCLUDE_LIST ${LLVM_DEMANGLE_PATH}/include)

# libstdcxx
# DAS - I have reservations about statically linking the c++ standard library,
#       but couldn't put together a more competent solution in a short amount
#       of time.  I suspect that it's fairly rare for customers to not have a
#       c++ runtime as part of their distro core, but containers can get pretty
#       small so who knows.  Either way, this is more about linking C++ from C
#       than it is about statically linking libc++/libstdc++
include(Findlibstdcxx)

## Elf utils
list(APPEND DDPROF_INCLUDE_LIST ${ELFUTILS_INCLUDE_LIST})

# Find the source files
aux_source_directory(src MAIN_SRC)
aux_source_directory(src/proto PROTO_SRC)

# Define all sources
set(DDPROF_GLOBAL_SRC
   ${DDPROF_GLOBAL_SRC}
   ${MAIN_SRC}
   ${PROTO_SRC})

set(DDPROF_LIBRARY_LIST ddprof-c llvm-demangle libstdc++.a dw elf libz.a Threads::Threads libprotobuf-c.a liblzma.a)

if (${JEMALLOC})
   list(PREPEND DDPROF_LIBRARY_LIST jemalloc)
endif()

# libcap, can be removed from version distributed to client 
list(APPEND DDPROF_LIBRARY_LIST libcap)
list(APPEND DDPROF_INCLUDE_LIST ${LIBCAP_INCLUDE_DIR})

# It is important to force most libraries as static
add_exe(ddprof
        ${DDPROF_GLOBAL_SRC}
        LIBRARIES ${DDPROF_LIBRARY_LIST}
        DEFINITIONS ${DDPROF_DEFINITION_LIST})
target_include_directories(ddprof PRIVATE ${DDPROF_INCLUDE_LIST})

message(STATUS "Install destination " ${CMAKE_INSTALL_PREFIX})
install(TARGETS ddprof)
