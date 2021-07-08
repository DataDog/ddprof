execute_process(COMMAND ${CMAKE_SOURCE_DIR}/tools/find_libstdcxx.sh
                OUTPUT_VARIABLE LIBSTDCXX_STATIC_LIB)
get_filename_component(LIBSTDCXX_DIR ${LIBSTDCXX_STATIC_LIB} DIRECTORY)
link_directories(${LIBSTDCXX_DIR})
