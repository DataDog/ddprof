// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <cstddef>
#include <cstdint>

namespace ddprof {

/// Maximum number of different watcher types
inline constexpr size_t kMaxTypeWatcher{10};

// Maximum depth for a single stack
inline constexpr size_t kMaxStackDepth{512};

// Sample stack size must a multiple of 8 and strictly inferior to 2^16
// Note that since maximum perf_event_hdr size is 2^16-1 and there are other
// data/headers in perf_event struct, actual maximum stack sample size returned
// as `size_stack` might be smaller then the requested size
// The type is uint32 to be consistent with the perf_event interface
// Check linux sources for a reference to the sample size check
inline constexpr uint32_t k_default_perf_stack_sample_size = 32000;

// considering sample size, we adjust the size of ring buffers.
// Following is considered as a minimum number of samples to be fit in the
// ring buffer.
inline constexpr auto k_min_number_samples_per_ring_buffer = 8;

inline constexpr int k_size_api_key = 32;

// Maximum number of profiled pids
// Exceeding a number of PIDs overloads file descriptors and memory
inline constexpr int k_default_max_profiled_pids{100};
inline constexpr int k_unlimited_max_profiled_pids{-1};

// Linux Inode type
using inode_t = uint64_t;

using SymbolIdx_t = int32_t;
inline constexpr SymbolIdx_t k_symbol_idx_null = -1;

using MapInfoIdx_t = int32_t;
inline constexpr MapInfoIdx_t k_mapinfo_idx_null = -1;
// Elf address (same as the address used with addr2line)
using ElfAddress_t = uint64_t;
// Offset types : add or subtract to address types
using Offset_t = ElfAddress_t;
// Absolute address (needs to be adjusted with the start address of binary)
using ProcessAddress_t = ElfAddress_t;
// Address within a binary : adjust doing (process_addr - module_start)
using FileAddress_t = ElfAddress_t;
// Elf word
using ElfWord_t = uint64_t;

} // namespace ddprof
