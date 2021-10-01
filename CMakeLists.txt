cmake_minimum_required(VERSION 3.19)

############################
### Global definitions   ###
############################

 project(DDProf
  LANGUAGES C CXX
  VERSION 0.6.3
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

include(CheckIPOSupported)
check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT error)
if( LTO_SUPPORTED )
   message(STATUS "IPO / LTO supported")
else()
   message(STATUS "IPO / LTO not supported: <${error}>")
endif()

##############
### Deps   ###
##############
# Define a dependency target to fetch all dependencies ahead of time (CI build time optim)

# libddprof
include(Findlibddprof)
message(STATUS "Libddprof Include directory " ${LIBDDPROF_INCLUDE_DIR})

include(Findelfutils)

#statsd
include(Finddogfood)

# llvm-demangle
include(FindLibLlvmDemangle)

add_custom_target(deps
                  DEPENDS llvm-deps ddprof-deps elfutils-deps dogfood-deps)

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

# c++ containers (MIT license) without pulling in boost (mainly flat set)
find_package(amc)

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

list(APPEND DDPROF_INCLUDE_LIST ${CMAKE_SOURCE_DIR}/include)

# statsd API
list(APPEND DDPROF_INCLUDE_LIST ${DOGFOOD_INCLUDE_DIR})

#libddprof
list(APPEND DDPROF_INCLUDE_LIST ${LIBDDPROF_INCLUDE_DIR})

# Check that we are on a linux system
message(STATUS "SYSTEM NAME " ${CMAKE_SYSTEM_NAME})
if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
   message(STATUS "BUILD WILL FAIL : NON LINUX SYSTEMS NOT HANDLED")
endif()

# Lzma
find_package(LibLZMA)
list(APPEND DDPROF_INCLUDE_LIST ${LIBLZMA_INCLUDE_DIRS})
get_filename_component(LIB_DIRNAME_LZMA ${LIBLZMA_LIBRARIES} DIRECTORY)
message(STATUS "LibLZMA found in following folder : " ${LIB_DIRNAME_LZMA})
link_directories(${LIB_DIRNAME_LZMA})

list(APPEND DDPROF_INCLUDE_LIST ${LLVM_DEMANGLE_PATH}/include)

## Elf utils
list(APPEND DDPROF_INCLUDE_LIST ${ELFUTILS_INCLUDE_LIST})

# Find the source files
aux_source_directory(src COMMON_SRC)
aux_source_directory(src/pprof PPROF_SRC)
aux_source_directory(src/exporter EXPORTER_SRC)
aux_source_directory(src/exe EXE_SRC)


# Define all sources
set(DDPROF_GLOBAL_SRC
   ${COMMON_SRC}
   ${PPROF_SRC}
   ${EXPORTER_SRC}
   ${EXE_SRC})


set(DDPROF_LIBRARY_LIST llvm-demangle dw elf libz.a Threads::Threads liblzma.a amc::amc)

if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
   # Static link of libstdc++ (in case it is not installed on customer side)
   list(PREPEND DDPROF_LIBRARY_LIST -static-libstdc++)
endif()

if (ON)
   # Add the rust library - Refactoring ongoing. OFF for now
   list(PREPEND DDPROF_LIBRARY_LIST DDProf::FFI)
   list(APPEND DDPROF_LIBRARY_LIST ${CMAKE_DL_LIBS})
endif()


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
# TODO: revert to c++ 11 (string_format is blocking)
set_property(TARGET ddprof PROPERTY CXX_STANDARD 14)
set_property(TARGET ddprof PROPERTY C_STANDARD 11)

# Link time optim
if( LTO_SUPPORTED AND "${CMAKE_BUILD_TYPE}" STREQUAL "Release")
   set_property(TARGET ddprof PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

message(STATUS "Install destination " ${CMAKE_INSTALL_PREFIX})
install(TARGETS ddprof)


#######################################
### Declaration of native library   ###
#######################################
option(BUILD_NATIVE_LIB "Build a library out of the native profiler" OFF)
   if (${BUILD_NATIVE_LIB})

   aux_source_directory(src/lib LIB_SRC)

   # Define all sources
   set(DDPROF_LIB_SRC
      ${COMMON_SRC}
      ${LIB_SRC})

   ## Libs to link
   set(NATIVE_LIB_LIBRARY_LIST llvm-demangle dw elf libz.a Threads::Threads liblzma.a amc::amc)
   list(APPEND NATIVE_LIB_INCLUDE_LIST ${LIBDDPROF_INCLUDE_DIR} ) # to be removed (unlikely.h)
   list(APPEND NATIVE_LIB_INCLUDE_LIST ${CMAKE_SOURCE_DIR}/include ${ELFUTILS_INCLUDE_LIST} ${DOGFOOD_INCLUDE_DIR}) # legit includes

   ## Create the lib
   add_library(ddprof-native
               ${DDPROF_LIB_SRC})
   set_property(TARGET ddprof-native PROPERTY CXX_STANDARD 14)
   set_property(TARGET ddprof-native PROPERTY C_STANDARD 11)
               
   set_target_properties(ddprof-native PROPERTIES VERSION ${PROJECT_VERSION})
   set_target_properties(ddprof-native PROPERTIES
                         COMPILE_DEFINITIONS DDPROF_NATIVE_LIB)

   # libcap, can be removed from version distributed to client 
   list(APPEND NATIVE_LIB_LIBRARY_LIST libcap)
   list(APPEND NATIVE_LIB_INCLUDE_LIST ${LIBCAP_INCLUDE_DIR})

   ## Headers we publish
   set(LIB_HEADERS include/ddprof.h include/ddprof_input.h include/lib/ddprof_output.hpp include/ipinfo_table.hpp include/mapinfo_table.hpp include/ddres.h include/ddres_def.h include/ddres_list.h include/ddres_helpers.h include/ddres_exception.hpp)
   set_target_properties(ddprof-native PROPERTIES PUBLIC_HEADER "${LIB_HEADERS}")

   target_include_directories(ddprof-native PRIVATE ${NATIVE_LIB_INCLUDE_LIST})

   target_link_libraries(ddprof-native PRIVATE ${NATIVE_LIB_LIBRARY_LIST})
   add_library(DDProf::Native ALIAS ddprof-native)
   install(TARGETS ddprof-native)


   option(ACCURACY_TEST "Enable accuracy test" OFF)
   if(${ACCURACY_TEST})
      add_subdirectory(test/self_unwind)
   endif()
endif()
