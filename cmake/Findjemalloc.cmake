# cmake-format: off
# Source : https://github.com/c9s/r3/blob/2.0/cmake/Modules/FindJemalloc.cmake
# Licence : MIT license
#
# - Try to find jemalloc headers and libraries.
#
# Usage of this module as follows:
#
#     find_package(JeMalloc)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  JEMALLOC_ROOT_DIR Set this variable to the root installation of
#                    jemalloc if the module has problems finding
#                    the proper installation path.
#
# Variables defined by this module:
#
#  JEMALLOC_FOUND             System has jemalloc libs/headers
#  JEMALLOC_SHARED_LIBRARIES  The jemalloc shared library/libraries
#  JEMALLOC_STATIC_LIBRARIES  The jemalloc static library/libraries
#  JEMALLOC_INCLUDE_DIR       The location of jemalloc headers
find_path(JEMALLOC_ROOT_DIR
    NAMES include/jemalloc/jemalloc.h
)

find_library(JEMALLOC_STATIC_LIBRARIES
    NAMES libjemalloc.a
    HINTS ${JEMALLOC_ROOT_DIR}/lib
)

find_library(JEMALLOC_SHARED_LIBRARIES
    NAMES libjemalloc.so
    HINTS ${JEMALLOC_ROOT_DIR}/lib
)

find_path(JEMALLOC_INCLUDE_DIR
    NAMES jemalloc/jemalloc.h
    HINTS ${JEMALLOC_ROOT_DIR}/include
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(jemalloc DEFAULT_MSG
    JEMALLOC_SHARED_LIBRARIES
    JEMALLOC_STATIC_LIBRARIES    
    JEMALLOC_INCLUDE_DIR
)

mark_as_advanced(
    JEMALLOC_ROOT_DIR
    JEMALLOC_SHARED_LIBRARIES
    JEMALLOC_STATIC_LIBRARIES    
    JEMALLOC_INCLUDE_DIR
)
if(jemalloc_FOUND)
   add_library(jemalloc::jemalloc_static STATIC IMPORTED)
   set_target_properties(jemalloc::jemalloc_static PROPERTIES
   INTERFACE_INCLUDE_DIRECTORIES "${JEMALLOC_INCLUDE_DIR}"
   IMPORTED_LINK_INTERFACE_LANGUAGES "C"
   IMPORTED_LOCATION "${JEMALLOC_STATIC_LIBRARIES}")

   add_library(jemalloc::jemalloc_shared SHARED IMPORTED)
   set_target_properties(jemalloc::jemalloc_shared PROPERTIES
   INTERFACE_INCLUDE_DIRECTORIES "${JEMALLOC_INCLUDE_DIR}"
   IMPORTED_LINK_INTERFACE_LANGUAGES "C"
   IMPORTED_LOCATION "${JEMALLOC_SHARED_LIBRARIES}")
endif()
# cmake-format: on
