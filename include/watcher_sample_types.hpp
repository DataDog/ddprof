// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

// Maps a watcher's event to pprof sample types for each aggregation mode.
// Values are ddog_prof_SampleType stored as uint32_t to avoid including
// <datadog/common.h> in this header. Static asserts in ddprof_pprof.cc
// verify the values match the libdatadog enum.
// count_types[pos] == k_stype_val_sample signals "no count companion".

#include "event_config.hpp"

#include <cstdint>

namespace ddprof {

struct WatcherSampleTypes {
  uint32_t sample_types[kNbEventAggregationModes]; // [kSumPos, kLiveSumPos]
  uint32_t count_types[kNbEventAggregationModes];  // companion counts
};

// ddog_prof_SampleType integer values for libdatadog v28.
// Stored as uint32_t to avoid including <datadog/common.h> here.
// Static asserts in ddprof_pprof.cc verify these against the actual enum.
inline constexpr uint32_t k_stype_val_sample = 37;       // SAMPLE
inline constexpr uint32_t k_stype_val_cpu_time = 4;      // CPU_TIME
inline constexpr uint32_t k_stype_val_cpu_samples = 5;   // CPU_SAMPLES
inline constexpr uint32_t k_stype_val_alloc_space = 3;   // ALLOC_SPACE
inline constexpr uint32_t k_stype_val_alloc_samples = 0; // ALLOC_SAMPLES
inline constexpr uint32_t k_stype_val_inuse_space = 28;  // INUSE_SPACE
inline constexpr uint32_t k_stype_val_inuse_objects = 27; // INUSE_OBJECTS

// Generic sample counting (hardware events, misc software events).
// clang-format off
inline constexpr WatcherSampleTypes k_stype_tracepoint = {
    {k_stype_val_sample,      k_stype_val_sample},
    {k_stype_val_sample,      k_stype_val_sample}};

// CPU: wall/cpu nanoseconds in sum mode, sample count in live mode.
inline constexpr WatcherSampleTypes k_stype_cpu = {
    {k_stype_val_cpu_time,    k_stype_val_cpu_samples},
    {k_stype_val_cpu_samples, k_stype_val_sample}};

// Allocation: bytes allocated / live bytes, with object-count companions.
inline constexpr WatcherSampleTypes k_stype_alloc = {
    {k_stype_val_alloc_space,   k_stype_val_inuse_space},
    {k_stype_val_alloc_samples, k_stype_val_inuse_objects}};
// clang-format on

} // namespace ddprof
