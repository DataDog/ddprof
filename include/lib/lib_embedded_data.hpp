// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "span.hpp"

namespace ddprof {
span<const std::byte> profiling_lib_data();

span<const std::byte> profiler_exe_data();

} // namespace ddprof