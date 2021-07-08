set(LLVM_DEMANGLE_PATH ${CMAKE_SOURCE_DIR}/vendor/llvm-demangle CACHE STRING "Path to the llvm-demangle directory")
set(LLVM_DEMANGLE_SVNROOT https://github.com/llvm/llvm-project/trunk/llvm CACHE STRING "GitHub URL for LLVM")

execute_process(COMMAND "${CMAKE_SOURCE_DIR}/tools/fetch_llvm_demangler.sh"
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                ERROR_VARIABLE PRINTOUT)
add_library(llvm-demangle STATIC
  src/demangle.cpp
  ${LLVM_DEMANGLE_SOURCES}
)
target_include_directories(llvm-demangle PUBLIC
  ${LLVM_DEMANGLE_PATH}/include
)
link_directories(${CMAKE_BUILD_DIRECTORY})
