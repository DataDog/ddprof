// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EmbeddedData {
  const char *data;
  size_t size;
  const char *digest;
} EmbeddedData;

EmbeddedData profiling_lib_data();
EmbeddedData profiler_exe_data();

#ifdef __cplusplus
}
#endif
