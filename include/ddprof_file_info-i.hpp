// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

namespace ddprof {
// unique identifier to serve as a key for Dso
typedef int32_t FileInfoId_t;
static const int k_file_info_undef = -1;
static const int k_file_info_error = 0;
} // namespace ddprof
