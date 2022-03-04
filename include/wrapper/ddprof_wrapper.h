// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int ddprof_start_profiling();
void ddprof_stop_profiling(int timeout_ms);

#ifdef __cplusplus
}
#endif