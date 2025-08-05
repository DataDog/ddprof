// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_cmdline.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <span>
#include <string_view>

namespace ddprof {

int arg_which(std::string_view str, std::span<const std::string_view> str_set) {
  auto it = std::ranges::find_if(str_set, [&str](std::string_view s) {
    return s.size() == str.size() &&
        std::equal(str.begin(), str.end(), s.begin(), s.end(),
                   [](char a, char b) {
                     return std::tolower(a) == std::tolower(b);
                   });
  });
  return (it == str_set.end()) ? -1 : std::distance(str_set.begin(), it);
}

bool arg_yes(std::string_view str) {
  static constexpr std::string_view yes_set[] = {"yes", "true", "on", "1"};
  return arg_which(str, yes_set) != -1;
}

bool arg_no(std::string_view str) {
  static constexpr std::string_view no_set[] = {"no", "false", "off", "0"};
  return arg_which(str, no_set) != -1;
}

} // namespace ddprof
