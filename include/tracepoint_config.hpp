// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stddef.h>
#include <string_view>

namespace ddprof {
// Returns the ID of the given Linux tracepoint, or -1 if an error occurs.
int64_t tracepoint_get_id(std::string_view global_name,
                          std::string_view tracepoint_name);
} // namespace ddprof
