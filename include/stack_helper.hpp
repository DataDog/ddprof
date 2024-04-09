// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"

namespace ddprof {
bool memory_read(ProcessAddress_t addr, ElfWord_t *result, int regno,
                 void *arg);
}
