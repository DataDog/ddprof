add_library(libunwind STATIC IMPORTED)
set_target_properties(libunwind PROPERTIES IMPORTED_LOCATION /usr/local/lib/libunwind.a
                                           INTERFACE_INCLUDE_DIRECTORIES /usr/local/include/)
