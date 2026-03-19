// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

// Maps a watcher's event to pprof sample types for each aggregation mode.
// k_stype_none signals "no sample/count type for this aggregation mode".
// Fields are uint32_t (not the enum) to allow k_stype_none = UINT32_MAX,
// which lies outside the valid enum range.

#include "event_config.hpp"

#include <cstdint>
#include <datadog/common.h>

namespace ddprof {

struct WatcherSampleTypes {
  uint32_t sample_types[kNbEventAggregationModes]; // [kSumPos, kLiveSumPos]
  uint32_t count_types[kNbEventAggregationModes];  // companion counts
};

// Sentinel: slot is unused for this aggregation mode.
inline constexpr uint32_t k_stype_none = UINT32_MAX;

// Tracepoints: one event = one sample, no count companion, no live mode.
// clang-format off
inline constexpr WatcherSampleTypes k_stype_tracepoint = {
    {DDOG_PROF_SAMPLE_TYPE_TRACEPOINT, k_stype_none},
    {k_stype_none,                     k_stype_none}};

// CPU: nanoseconds in sum mode only — no live profile for CPU.
inline constexpr WatcherSampleTypes k_stype_cpu = {
    {DDOG_PROF_SAMPLE_TYPE_CPU_TIME,    k_stype_none},
    {DDOG_PROF_SAMPLE_TYPE_CPU_SAMPLES, k_stype_none}};

// Allocation: bytes allocated / live bytes, with object-count companions.
inline constexpr WatcherSampleTypes k_stype_alloc = {
    {DDOG_PROF_SAMPLE_TYPE_ALLOC_SPACE,   DDOG_PROF_SAMPLE_TYPE_INUSE_SPACE},
    {DDOG_PROF_SAMPLE_TYPE_ALLOC_SAMPLES, DDOG_PROF_SAMPLE_TYPE_INUSE_OBJECTS}};
// clang-format on

} // namespace ddprof
