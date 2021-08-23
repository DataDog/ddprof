# libddprof : get these from the build of libddprof

## https://gitlab.ddbuild.io/DataDog/libddprof/-/jobs/80828557
set(VER_LIBDDPROF "30fa153c" CACHE STRING "libddprof version")
set(SHA256_LIBDDPROF "7eda21aa7644c3b277d67380fdd9af31c7b5e861670ddb34da9dd43e78ee7580" CACHE STRING "libddprof sha256")

set(LIBDDPROF_REL_LIB ${CMAKE_SOURCE_DIR}/vendor/libddprof/deliverables/RelWithDebInfo/lib64/libddprof-c.a)
set(LIBDDPROF_INCLUDE_DIR
    ${CMAKE_SOURCE_DIR}/vendor/libddprof/deliverables/RelWithDebInfo/include)
set(LIBDDPROF_VERSION_FILE ${CMAKE_SOURCE_DIR}/vendor/libddprof/version.txt)

# Only setting the lib as output (though headers are produced)
set(LIBDDPROF_FILES
  ${LIBDDPROF_REL_LIB}
  ${LIBDDPROF_VERSION_FILE})

add_custom_command(OUTPUT ${LIBDDPROF_FILES}
                  COMMAND "${CMAKE_SOURCE_DIR}/tools/fetch_libddprof.sh" ${VER_LIBDDPROF} ${SHA256_LIBDDPROF} ${VENDOR_PATH}
                  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                  COMMENT "Fetching libddprof version ${VER_LIBDDPROF}...")
add_custom_target(ddprof-deps DEPENDS ${LIBDDPROF_FILES})


# Version check 
# The dependency is downloaded as a build cache in the vendor directory
# When importing the library, also check the version to make sure we are coherent (lib can be fetched in previous builds)
# A file is created in the build dir to detect when to re-run the check
# Warning : CMakeCache.txt will hold variables like VER_LIBDDPROF. Clean it if you want to update versions (or clean build tree)
set(LIBDDPROF_VERSION_CHECK_FILE ${CMAKE_BINARY_DIR}/libddprof_version_check.txt)
add_custom_command(OUTPUT ${LIBDDPROF_VERSION_CHECK_FILE}
                   COMMAND "${CMAKE_SOURCE_DIR}/tools/check_libddprof_version.sh" ${LIBDDPROF_VERSION_FILE} ${VER_LIBDDPROF} ${LIBDDPROF_VERSION_CHECK_FILE}
                   DEPENDS ddprof-deps
                   ARGS ${LIBDDPROF_VERSION_FILE})
add_custom_target(ddprof-version DEPENDS ${LIBDDPROF_VERSION_CHECK_FILE})

add_library(ddprof-c STATIC IMPORTED)
set_property(TARGET ddprof-c PROPERTY
             IMPORTED_LOCATION ${LIBDDPROF_REL_LIB})
add_dependencies(ddprof-c ddprof-version)
