// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <cstddef>
#include <cstdint>

#include "perf_archmap.h"
#include "span.hpp"

/** Save registers and stack for remote unwinding
 * Return saved stack size */
size_t save_context(ddprof::span<uint64_t, PERF_REGS_COUNT> regs,
                    ddprof::span<std::byte> buffer) __attribute__((noinline));
