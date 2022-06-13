# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

cmake_minimum_required(VERSION 3.19)

############################
### Global definitions   ###
############################

 project(DDProf
  LANGUAGES C CXX
  VERSION 0.8.2
  DESCRIPTION "Datadog's native profiler for Linux"
)

message(STATUS "Compiler ID: ${CMAKE_C_COMPILER_ID}")
message(STATUS "System processor (platform): ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "SYSTEM NAME " ${CMAKE_SYSTEM_NAME})
if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
   message(FATAL_ERROR "Non Linux systems are not handled in ddprof")
endif()

# Debug command to get gcc command lines
# set(CMAKE_VERBOSE_MAKEFILE on)

# Define the include path of cmake scripts
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(ExtendBuildTypes)

# Allow cppcheck to build off of build commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default value
message(STATUS "Build type set to " ${CMAKE_BUILD_TYPE})

# Check for pthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

#helper functions (defines add_exe)
include(Helperfunc)

# path to external dependencies
set(VENDOR_EXTENSION "" CACHE STRING "Extension to allow builds with different libc")
set(VENDOR_PATH "${CMAKE_SOURCE_DIR}/vendor${VENDOR_EXTENSION}" CACHE FILEPATH "Path to the vendor directory")
message(STATUS "Vendor path set to " ${VENDOR_PATH})

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

# libddprof
include(Findlibddprof)

# elfutils
include(Findelfutils)


#######################
### Static analysis ###
#######################
include(ClangTidy)
include(CppcheckConfig)
include(Format)

##################################
### Libraries (needed by ut)   ###
##################################
find_package(PkgConfig REQUIRED)

option(DDPROF_JEMALLOC "Enable jemalloc stats" OFF)
if (${DDPROF_JEMALLOC})
   #jemalloc for debug
   include(Jemalloc)
   message(STATUS "Adding jemalloc for DBG purpose" ${JEMALLOC_ROOT_DIR})
   add_compile_definitions("DBG_JEMALLOC")
endif()

# Install lib cap to retrieve capabilities
include(Findlibcap)

# llvm demangler
add_subdirectory(third_party/llvm)

###################
### Benchmarks  ###
###################
option(BUILD_BENCHMARKS "Enable benchmarks" OFF)
if (${BUILD_BENCHMARKS})
  add_subdirectory(bench/collatz)
endif()

###############################
### Declaration of DDProf   ###
###############################
# Compile time definitions 
string(TOLOWER ${CMAKE_PROJECT_NAME} CMAKE_PROJECT_NAME_LC)
list(APPEND DDPROF_DEFINITION_LIST "MYNAME=\"${CMAKE_PROJECT_NAME_LC}\"")

if("${CMAKE_BUILD_TYPE}" STREQUAL "Release" OR "${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
   list(APPEND DDPROF_DEFINITION_LIST "DDPROF_OPTIM=1")
endif()

include(Version)

# Leave frame pointers to help with profiling
string(APPEND CMAKE_C_FLAGS " ${FRAME_PTR_FLAG}")
string(APPEND CMAKE_CXX_FLAGS " ${FRAME_PTR_FLAG}")

list(APPEND DDPROF_INCLUDE_LIST ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/third_party)

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

set(DDPROF_LIBRARY_LIST llvm-demangle ${ELFUTILS_LIBRARIES} Threads::Threads)

if (ON)
   # Add the rust library - Refactoring ongoing. OFF for now
   list(PREPEND DDPROF_LIBRARY_LIST DDProf::FFI)
endif()


if (${DDPROF_JEMALLOC})
   list(PREPEND DDPROF_LIBRARY_LIST jemalloc)
endif()

# libcap, can be removed from version distributed to client 
list(APPEND DDPROF_LIBRARY_LIST libcap)
list(APPEND DDPROF_INCLUDE_LIST ${LIBCAP_INCLUDE_DIR})

set(DD_PROFILING_SOURCES src/lib/dd_profiling.cc src/daemonize.cc src/ddprof_cmdline.c src/logger.c src/logger_setup.cc 
                         src/ipc.cc src/pevent_lib.cc src/perf.cc src/perf_ringbuffer.cc src/perf_watcher.cc src/ringbuffer_utils.cc src/sys_utils.cc
                         src/user_override.c src/ddres_list.c src/savecontext.cc src/saveregisters.cc src/lib/malloc_wrapper.cc 
                         src/lib/allocation_tracker.cc)

add_library(dd_profiling-embedded SHARED ${DD_PROFILING_SOURCES})
target_include_directories(dd_profiling-embedded PUBLIC ${CMAKE_SOURCE_DIR}/include/lib ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/third_party)
set_target_properties(dd_profiling-embedded PROPERTIES
   PUBLIC_HEADER "${CMAKE_SOURCE_DIR}/include/lib/dd_profiling.h")

# Link libstdc++/libgcc statically and export only profiler API
static_link_cxx(TARGET dd_profiling-embedded)
set_target_properties(dd_profiling-embedded PROPERTIES LINK_DEPENDS "${CMAKE_SOURCE_DIR}/cmake/dd_profiling.version")
target_link_options(dd_profiling-embedded PRIVATE "LINKER:--version-script=${CMAKE_SOURCE_DIR}/cmake/dd_profiling.version")
# Fix for link error in sanitizeddebug build mode with gcc:
# /usr/bin/ld: ./libdd_profiling.so: undefined reference to `__dynamic_cast'
# /usr/bin/ld: ./libdd_profiling.so: undefined reference to `typeinfo for __cxxabiv1::__vmi_class_type_info'
# The cause of the error is that gcc puts `-lstdc++` before `-lubsan` in the linker invocation
# Workaround is add another `-lstdc++` after `-lubsan` at the end, we cannot use `-static-libstdc++` because it does not force gcc to another `-lstdc++` at the end
target_link_libraries(dd_profiling-embedded PRIVATE "$<$<AND:$<C_COMPILER_ID:GNU>,$<CONFIG:SanitizedDebug>>:-Wl,-Bstatic;-lubsan;-lasan;-lstdc++;-Wl,-Bdynamic>")
target_link_libraries(dd_profiling-embedded PUBLIC dl pthread)

set(LIBDDPROFILING_OBJECT "libdd_profiling.o")
add_custom_command(OUTPUT ${LIBDDPROFILING_OBJECT}
  # taken from https://dvdhrm.wordpress.com/2013/03/08/linking-binary-data/
  COMMAND ld -r -o ${LIBDDPROFILING_OBJECT} -z noexecstack --format=binary $<TARGET_FILE_NAME:dd_profiling-embedded>
  COMMAND objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents ${LIBDDPROFILING_OBJECT}
  DEPENDS dd_profiling-embedded
)
add_library(libddprofiling_object OBJECT IMPORTED GLOBAL)
set_target_properties(libddprofiling_object PROPERTIES IMPORTED_OBJECTS "${LIBDDPROFILING_OBJECT}")

# It is important to force most libraries as static
add_exe(ddprof
        ${DDPROF_GLOBAL_SRC} $<TARGET_OBJECTS:libddprofiling_object>
        LIBRARIES ${DDPROF_LIBRARY_LIST}
        DEFINITIONS ${DDPROF_DEFINITION_LIST})
target_include_directories(ddprof PRIVATE ${DDPROF_INCLUDE_LIST})
static_link_cxx(TARGET ddprof)

# Link time optim
if(LTO_SUPPORTED AND "${CMAKE_BUILD_TYPE}" STREQUAL "Release")
   set_property(TARGET ddprof PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

message(STATUS "Install destination " ${CMAKE_INSTALL_PREFIX})
install(FILES LICENSE LICENSE-3rdparty.csv LICENSE.LGPLV3 NOTICE DESTINATION ddprof)

set(DDPROF_EXE_OBJECT "ddprof.o")
add_custom_command(OUTPUT ${DDPROF_EXE_OBJECT}
  # taken from https://dvdhrm.wordpress.com/2013/03/08/linking-binary-data/
  COMMAND ld -r -o ${DDPROF_EXE_OBJECT} -z noexecstack --format=binary $<TARGET_FILE_NAME:ddprof>
  COMMAND objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents ${DDPROF_EXE_OBJECT}
  DEPENDS ddprof
)

add_library(ddprof_exe_object OBJECT IMPORTED GLOBAL)
set_target_properties(ddprof_exe_object PROPERTIES IMPORTED_OBJECTS "${DDPROF_EXE_OBJECT}")

add_library(dd_profiling-static STATIC ${DD_PROFILING_SOURCES} $<TARGET_OBJECTS:ddprof_exe_object>)
set_target_properties(dd_profiling-static PROPERTIES OUTPUT_NAME dd_profiling)
target_include_directories(dd_profiling-static PUBLIC ${CMAKE_SOURCE_DIR}/include/lib ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/third_party)
set_target_properties(dd_profiling-static PROPERTIES
   PUBLIC_HEADER "${CMAKE_SOURCE_DIR}/include/lib/dd_profiling.h")
target_link_libraries(dd_profiling-static PUBLIC dl pthread)

add_library(dd_profiling-shared SHARED ${DD_PROFILING_SOURCES} $<TARGET_OBJECTS:ddprof_exe_object>)
set_target_properties(dd_profiling-shared PROPERTIES OUTPUT_NAME dd_profiling)
target_include_directories(dd_profiling-shared PUBLIC ${CMAKE_SOURCE_DIR}/include/lib ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/third_party)
set_target_properties(dd_profiling-shared PROPERTIES
   PUBLIC_HEADER "${CMAKE_SOURCE_DIR}/include/lib/dd_profiling.h")

# Link libstdc++/libgcc statically and export only profiler API
static_link_cxx(TARGET dd_profiling-shared)
set_target_properties(dd_profiling-shared PROPERTIES LINK_DEPENDS "${CMAKE_SOURCE_DIR}/cmake/dd_profiling.version")
target_link_options(dd_profiling-shared PRIVATE "LINKER:--version-script=${CMAKE_SOURCE_DIR}/cmake/dd_profiling.version")
# Fix for link error in sanitizeddebug build mode with gcc:
# /usr/bin/ld: ./libdd_profiling.so: undefined reference to `__dynamic_cast'
# /usr/bin/ld: ./libdd_profiling.so: undefined reference to `typeinfo for __cxxabiv1::__vmi_class_type_info'
# The cause of the error is that gcc puts `-lstdc++` before `-lubsan` in the linker invocation
# Workaround is add another `-lstdc++` after `-lubsan` at the end, we cannot use `-static-libstdc++` because it does not force gcc to another `-lstdc++` at the end
target_link_libraries(dd_profiling-shared PRIVATE "$<$<AND:$<C_COMPILER_ID:GNU>,$<CONFIG:SanitizedDebug>>:-Wl,-Bstatic;-lubsan;-lasan;-lstdc++;-Wl,-Bdynamic>")
target_link_libraries(dd_profiling-shared PUBLIC dl pthread)

install(TARGETS ddprof dd_profiling-static dd_profiling-shared
        RUNTIME DESTINATION ddprof/bin
        LIBRARY DESTINATION ddprof/lib
        ARCHIVE DESTINATION ddprof/lib
        PUBLIC_HEADER DESTINATION ddprof/include)

#######################################
### Declaration of native library   ###
#######################################
option(BUILD_NATIVE_LIB "Build a library out of the native profiler" OFF)
if (${BUILD_NATIVE_LIB})

   # Define all sources
   set(DDPROF_LIB_SRC
      ${COMMON_SRC}
      src/lib/ddprof_output.cc)

   ## Libs to link
   set(NATIVE_LIB_LIBRARY_LIST llvm-demangle ${ELFUTILS_LIBRARIES} Threads::Threads)

   if (${DDPROF_JEMALLOC})
      list(PREPEND NATIVE_LIB_LIBRARY_LIST jemalloc)
   endif()

   ## Create the lib
   add_library(ddprof-native
               ${DDPROF_LIB_SRC})
   
   set_target_properties(ddprof-native PROPERTIES VERSION ${PROJECT_VERSION})
   set_target_properties(ddprof-native PROPERTIES
                         COMPILE_DEFINITIONS DDPROF_NATIVE_LIB)
                         
   # libcap, can be removed from version distributed to client 
   list(APPEND NATIVE_LIB_LIBRARY_LIST libcap)

   target_include_directories(ddprof-native PRIVATE ${DDPROF_INCLUDE_LIST})

   target_link_libraries(ddprof-native PRIVATE ${NATIVE_LIB_LIBRARY_LIST})
   add_library(DDProf::Native ALIAS ddprof-native)


   option(ACCURACY_TEST "Enable accuracy test" OFF)
   if(${ACCURACY_TEST})
      add_subdirectory(test/self_unwind)
   endif()
endif()

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
