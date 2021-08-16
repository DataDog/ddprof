# libddprof : get these from the build of libddprof

## https://gitlab.ddbuild.io/DataDog/libddprof/-/jobs/76775503
set(VER_LIBDDPROF "ff9bd540" CACHE STRING "libddprof version")
set(SHA256_LIBDDPROF "b35a2977e0b279ae709d051e600de1741cbc6da64eceea0c643dfd0b0903ee77" CACHE STRING "libddprof sha256")

set(VENDOR_PATH ${CMAKE_SOURCE_DIR}/vendor CACHE STRING " Path to the vendor directory")

set(LIBDDPROF_REL_LIB ${CMAKE_SOURCE_DIR}/vendor/libddprof/RelWithDebInfo/lib64/libddprof-c.a)
set(LIBDDPROF_INCLUDE_DIR
    ${CMAKE_SOURCE_DIR}/vendor/libddprof/RelWithDebInfo/include)

# Only setting the lib as output (though headers are produced)
set(LIBDDPROF_FILES
  ${LIBDDPROF_REL_LIB})

add_custom_command(OUTPUT ${LIBDDPROF_FILES}
                  COMMAND "${CMAKE_SOURCE_DIR}/tools/fetch_libddprof.sh"
                  ARGS ${VER_LIBDDPROF} ${SHA256_LIBDDPROF} ${VENDOR_PATH}
                  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                  COMMENT "Fetching libddprof")

add_custom_target(ddprof-deps DEPENDS ${LIBDDPROF_FILES})

add_library(ddprof-c STATIC IMPORTED)
set_property(TARGET ddprof-c PROPERTY
             IMPORTED_LOCATION ${LIBDDPROF_REL_LIB})
add_dependencies(ddprof-c ddprof-deps)
