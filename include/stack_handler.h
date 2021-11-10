// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "unwind_output.h"

typedef struct DDProfContext DDProfContext;

// Callback on every stack
typedef struct StackHandler {
  bool (*apply)(const UnwindOutput *unwind_output, const DDProfContext *ctx,
                void *callback_ctx, int perf_opt_idx);
  void *callback_ctx; // user state to be used in callback
} StackHandler;
