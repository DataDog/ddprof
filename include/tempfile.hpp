// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres_def.hpp"

#include <span>
#include <string>
#include <string_view>
#include <sys/stat.h>

namespace ddprof {
DDRes get_or_create_temp_file(std::string_view prefix,
                              std::span<const std::byte> data, mode_t mode,
                              std::string &path);

DDRes create_temp_file(std::string_view prefix, std::span<const std::byte> data,
                       mode_t mode, std::string &path);
} // namespace ddprof
