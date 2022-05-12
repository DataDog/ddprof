// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "span.hpp"

namespace ddprof {
using Buffer = ddprof::span<std::byte>;
using ConstBuffer = ddprof::span<const std::byte>;
} // namespace ddprof
