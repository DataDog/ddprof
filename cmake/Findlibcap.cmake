# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

add_library(libcap STATIC IMPORTED)

find_library(LIBCAP_LIBRARY_PATH 
             libcap.a)

set_target_properties(libcap PROPERTIES IMPORTED_LOCATION "${LIBCAP_LIBRARY_PATH}")

find_path (LIBCAP_INCLUDE_DIR
            NAMES
            sys/capability.h)
