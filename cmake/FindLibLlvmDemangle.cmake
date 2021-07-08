# DAS - Technically, all of this is already specified in the CMakeLists.txt
# provided in the LLVM demangling source dir, but I can't be bothered to mock
# their add_llvm_component cmake definition

set(LLVM_DEMANGLE_PATH ${CMAKE_SOURCE_DIR}/vendor/llvm CACHE STRING " Path to the llvm-demangle directory")
set(LLVM_DEMANGLE_SVNROOT https://github.com/llvm/llvm-project/trunk/llvm CACHE STRING " GitHub URL for LLVM")
set(LLVM_DEMANGLE_SRC ${CMAKE_SOURCE_DIR}/vendor/llvm/lib/Demangle CACHE STRING "Path to the llvm-demangle sources")

execute_process(COMMAND "${CMAKE_SOURCE_DIR}/tools/fetch_llvm_demangler.sh"
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                ERROR_VARIABLE PRINTOUT)

add_library(llvm-demangle STATIC
  src/demangle.cpp
  ${LLVM_DEMANGLE_SRC}/Demangle.cpp
  ${LLVM_DEMANGLE_SRC}/ItaniumDemangle.cpp
  ${LLVM_DEMANGLE_SRC}/MicrosoftDemangle.cpp
  ${LLVM_DEMANGLE_SRC}/MicrosoftDemangleNodes.cpp
  ${LLVM_DEMANGLE_SRC}/RustDemangle.cpp
)
target_include_directories(llvm-demangle PUBLIC
  ${LLVM_DEMANGLE_PATH}/include
)
