// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "container_id_defs.hpp"
#include "ddprof_defs.hpp"
#include "ddres_def.hpp"

#include <optional>
#include <string>

namespace ddprof {
using ContainerId = std::optional<std::string>;
// Extract container id information
// Expects the path to the /proc/<PID>/cgroup file
DDRes extract_container_id(const std::string &filepath,
                           ContainerId &container_id);

} // namespace ddprof
