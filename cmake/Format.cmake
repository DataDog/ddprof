# Unless explicitly stated otherwise all files in this repository are licensed under the Apache
# License Version 2.0. This product includes software developed at Datadog
# (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

add_custom_target(format COMMAND ${CMAKE_SOURCE_DIR}/tools/style-check.sh)

add_custom_target(format-apply COMMAND ${CMAKE_SOURCE_DIR}/tools/style-check.sh apply)

add_custom_target(help-generate COMMAND ${CMAKE_SOURCE_DIR}/tools/help_generate.sh -b ${CMAKE_BINARY_DIR})
