// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <span>
#include <string_view>

namespace ddprof {

/// Returns index to element that matches str (case insensitive), otherwise -1
int arg_which(std::string_view str, std::span<const std::string_view> str_set);

bool arg_yes(std::string_view str);
bool arg_no(std::string_view str);

} // namespace ddprof
