// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

extern "C" {
#include "datadog/profiling.h"
}

#include "string_view.hpp"

#include <string_view>
#include <span>

inline ddog_CharSlice to_CharSlice(std::span <const unsigned char>span) {
  return {.ptr = reinterpret_cast<const char *>(span.data()), .len = span.size()};
}

inline ddog_CharSlice to_CharSlice(std::string_view str) {
  return {.ptr = str.data(), .len = str.size()};
}

inline ddog_CharSlice to_CharSlice(string_view slice) {
  return {.ptr = slice.ptr, .len = slice.len};
}

