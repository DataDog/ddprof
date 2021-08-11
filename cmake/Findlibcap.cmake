add_library(libcap STATIC IMPORTED)

find_library(LIBCAP_LIBRARY_PATH 
             libcap.a)

set_target_properties(libcap PROPERTIES IMPORTED_LOCATION "${LIBCAP_LIBRARY_PATH}")

find_path (LIBCAP_INCLUDE_DIR
            NAMES
            sys/capability.h)
