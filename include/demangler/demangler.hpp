// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <string_view>

namespace Demangler {

// Functions
std::string demangle(const std::string_view &mangled);

} // namespace Demangler
