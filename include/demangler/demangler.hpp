// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <string>
#include <string_view>

namespace Demangler {

// Functions
bool has_hash(const std::string &str);
bool is_probably_rust_legacy(const std::string &str);
std::string rust_demangle(const std::string &str);
std::string demangle(const std::string &mangled);

} // namespace Demangler
