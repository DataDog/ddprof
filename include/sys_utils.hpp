// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddres_def.h"
#include <string_view>

namespace ddprof {

DDRes sys_perf_event_paranoid(int32_t &val);

DDRes sys_read_int_from_file(const char *filename, int32_t &val);
} // namespace ddprof
