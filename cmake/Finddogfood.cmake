# Header only statsd api

set(DOGFOOD_INCLUDE_DIR
    ${CMAKE_SOURCE_DIR}/vendor/DogFood/)

set(DOGFOOD_FILES
  ${DOGFOOD_INCLUDE_DIR}/DogFood.hpp)

add_custom_command(OUTPUT ${DOGFOOD_FILES}
                    COMMAND "${CMAKE_SOURCE_DIR}/tools/fetch_dogfood.sh" "v2.0" "${VENDOR_PATH}"
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    COMMENT "Fetching libddprof version ${VER_LIBDDPROF}...")
add_custom_target(dogfood-deps DEPENDS ${DOGFOOD_FILES})
