// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "span.hpp"

#include <string>

namespace ddprof {
using BuildIdSpan = ddprof::span<const unsigned char>;
using BuildIdStr = std::string;

BuildIdStr format_build_id(BuildIdSpan build_id_span);

} // namespace ddprof
