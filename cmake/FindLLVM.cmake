find_package(LLVM CONFIG 17)

add_library(LLVMHeaders INTERFACE IMPORTED)
target_include_directories(LLVMHeaders INTERFACE ${LLVM_INCLUDE_DIRS})
