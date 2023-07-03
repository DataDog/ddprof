set(JUNCTION_INSTALL_PATH /home/r1viollet/junction-install)
# Create an imported static library target
add_library(junction STATIC IMPORTED)
set(JUNCTION_INCLUDE_DIRECTORY ${JUNCTION_INSTALL_PATH}/include)
# Set the library's imported location to the actual library file's location
set_target_properties(junction PROPERTIES IMPORTED_LOCATION ${JUNCTION_INSTALL_PATH}/lib/libjunction.a)
