// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <string_view>

// default container_id value in case of:
// - error example when PID is no longer available
// - we did not lookup (for perf reasons), so this is really unknown
constexpr static std::string_view k_container_id_unknown = "unknown";
// no container_id was found within the cgroup file
constexpr static std::string_view k_container_id_none = "none";
