# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

# Run commands before automatic detection of compiler
# Example with :
## cmake -C init_cache.cmake
set(CMAKE_C_COMPILER CACHE FILEPATH clang-12)
set(CMAKE_CXX_COMPILER CACHE FILEPATH clang-cpp-12)
