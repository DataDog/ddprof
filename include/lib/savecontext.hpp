// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "ddprof_base.hpp"
#include "perf_archmap.hpp"

namespace ddprof {

/**Retrieve stack bounds from current thread.
   Return an empty span in case of failure.*/
DDPROF_NOIPO std::span<const std::byte> retrieve_stack_bounds();

/** Save registers and stack for remote unwinding
 * Return saved stack size */
DDPROF_NOIPO size_t
save_context(std::span<const std::byte> stack_bounds,
             std::span<uint64_t, k_perf_register_count> regs,
             std::span<std::byte> buffer);

} // namespace ddprof
