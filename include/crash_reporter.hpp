// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "ddres_def.hpp"

#include "unwind_state.hpp"

#include <span>
#include <unistd.h>

namespace ddprof {

struct ExporterInput;

bool report_crash(pid_t pid, pid_t tid, const ExporterInput &export_input);

} // namespace ddprof
