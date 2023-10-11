// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

namespace ddprof {
// unique identifier to serve as a key for Dso
using FileInfoId_t = int32_t;
inline constexpr int k_file_info_undef = -1;
inline constexpr int k_file_info_error = 0;
inline constexpr int k_file_info_dd_profiling = 1;
} // namespace ddprof
