// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <memory>
#include <stdexcept>
#include <string>

// Thanks to @iFreilicht for this c++ 11 version
// licensed under CC0 1.0
// source :
// https://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf
namespace ddprof {

template <typename... Args>
std::string string_format(const std::string &format, Args... args) {
  int size_s = std::snprintf(nullptr, 0, format.c_str(), args...) +
      1; // Extra space for '\0'
  if (size_s <= 0) {
    throw std::runtime_error("Error during formatting.");
  }
  auto size = static_cast<size_t>(size_s);
#if __cplusplus < 201300
  auto buf = std::unique_ptr<char[]>(new char[size]);
#else
  auto buf = std::make_unique<char[]>(size);
#endif
  std::snprintf(buf.get(), size, format.c_str(), args...);
  return std::string(buf.get(),
                     buf.get() + size - 1); // We don't want the '\0' inside
}

} // namespace ddprof
