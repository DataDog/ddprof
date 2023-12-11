# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# DAS - Technically, all of this is already specified in the CMakeLists.txt
# provided in the LLVM demangling source dir, but I can't be bothered to mock
# their add_llvm_component cmake definition

# Defines :
# target : llvm-demangle

set(LLVM_DEMANGLE_SRC lib/Demangle)

set(LLVM_DEMANGLE_SRC_FILES
    ${LLVM_DEMANGLE_SRC}/Demangle.cpp
    ${LLVM_DEMANGLE_SRC}/ItaniumDemangle.cpp
    ${LLVM_DEMANGLE_SRC}/MicrosoftDemangle.cpp
    ${LLVM_DEMANGLE_SRC}/MicrosoftDemangleNodes.cpp
    ${LLVM_DEMANGLE_SRC}/RustDemangle.cpp
    ${LLVM_DEMANGLE_SRC}/DLangDemangle.cpp
    )

# add_library(llvm-demangle STATIC
#             ${LLVM_DEMANGLE_SRC_FILES})

# # disable warnings for third party
# target_compile_options(llvm-demangle PRIVATE "-w")
# disable_clangtidy(llvm-demangle)

# target_include_directories(llvm-demangle PUBLIC include)

add_library(llvm-demangle INTERFACE)
target_link_libraries(llvm-demangle INTERFACE LLVMDemangle)
target_link_libraries(llvm-demangle INTERFACE LLVMHeaders)
