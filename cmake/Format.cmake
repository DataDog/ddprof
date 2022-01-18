# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

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
