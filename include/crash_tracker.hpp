// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <string>
#include <vector>

namespace ddprof {

bool install_crash_tracker(const std::string &handler_exe,
                           const std::vector<std::string> &handler_args);

} // namespace ddprof
