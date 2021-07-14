add_library(libddprof STATIC IMPORTED)
#/usr/local/lib64/ on centos
find_library(LIBDDPROF_LIBRARY_PATH 
                ddprof
              HINTS 
                ${CMAKE_SOURCE_DIR}/vendor/libddprof/RelWithDebInfo/lib64
              PATHS
                ${CMAKE_SOURCE_DIR}/vendor/libddprof/RelWithDebInfo/lib64)

set_target_properties(libddprof PROPERTIES IMPORTED_LOCATION "${LIBDDPROF_LIBRARY_PATH}")

find_path (LIBDDPROF_INCLUDE_DIR
        NAMES
            ddprof/dd_send.h
        HINTS 
            ${CMAKE_SOURCE_DIR}/vendor/libddprof/RelWithDebInfo/include
        PATHS
            ${CMAKE_SOURCE_DIR}/vendor/libddprof/RelWithDebInfo/include
        ENV CPATH) # PATH and INCLUDE will also work
