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
#  JEMALLOC_LIBRARIES         The jemalloc library/libraries
#  JEMALLOC_INCLUDE_DIR       The location of jemalloc headers
find_path(JEMALLOC_ROOT_DIR
    NAMES include/jemalloc/jemalloc.h
)

find_library(JEMALLOC_LIBRARIES
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
find_package_handle_standard_args(JeMalloc DEFAULT_MSG
    JEMALLOC_LIBRARIES
    JEMALLOC_SHARED_LIBRARIES
    JEMALLOC_INCLUDE_DIR
)

mark_as_advanced(
    JEMALLOC_ROOT_DIR
    JEMALLOC_LIBRARIES
    JEMALLOC_INCLUDE_DIR
)
if(JeMalloc_FOUND AND NOT (TARGET JeMalloc::JeMalloc))
   add_library(JeMalloc::JeMalloc STATIC IMPORTED)
   set_target_properties(JeMalloc::JeMalloc PROPERTIES
   INTERFACE_INCLUDE_DIRECTORIES "${JEMALLOC_INCLUDE_DIR}"
   IMPORTED_LINK_INTERFACE_LANGUAGES "C"
   IMPORTED_LOCATION "${JEMALLOC_LIBRARIES}")

   add_library(JeMalloc::JeMallocShared SHARED IMPORTED)
   set_target_properties(JeMalloc::JeMallocShared PROPERTIES
   INTERFACE_INCLUDE_DIRECTORIES "${JEMALLOC_INCLUDE_DIR}"
   IMPORTED_LINK_INTERFACE_LANGUAGES "C"
   IMPORTED_LOCATION "${JEMALLOC_SHARED_LIBRARIES}")
endif()
# cmake-format: on
