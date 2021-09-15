#pragma once

#include "unwind_output.h"

typedef struct DDProfContext DDProfContext;

// Callback on every stack
typedef struct StackHandler {
  bool (*apply)(const UnwindOutput *unwind_output, const DDProfContext *ctx,
                int perf_opt_idx);
} StackHandler;
