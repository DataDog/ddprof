

add_custom_target(
    format
    COMMAND ${CMAKE_SOURCE_DIR}/tools/style-check.sh
    --error-exitcode=1 
    )

add_custom_target(
    format-apply
    COMMAND ${CMAKE_SOURCE_DIR}/tools/style-check.sh apply
    --error-exitcode=1 
    )
