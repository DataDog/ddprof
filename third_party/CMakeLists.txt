add_subdirectory(CLI)
add_subdirectory(llvm)

# bpf
include(ExternalProject)
# Create a libbpf-build target
ExternalProject_Add(libbpf
        PREFIX libbpf
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libbpf/src
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make
        BUILD_STATIC_ONLY=1
        OBJDIR=${CMAKE_CURRENT_BINARY_DIR}/libbpf/libbpf
        DESTDIR=${CMAKE_CURRENT_BINARY_DIR}/libbpf
        INCLUDEDIR=
        LIBDIR=
        UAPIDIR=
        install install_uapi_headers
        BUILD_IN_SOURCE TRUE
        INSTALL_COMMAND ""
        STEP_TARGETS build
        )

# Create a bpftool-build target
ExternalProject_Add(bpftool
        PREFIX bpftool
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bpftool/src
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make bootstrap
        OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/bpftool/
        BUILD_IN_SOURCE TRUE
        INSTALL_COMMAND ""
        STEP_TARGETS build
        )
