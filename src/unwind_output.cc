// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <stdio.h>
#include <string.h>

#include "unwind_output.hpp"

void uw_output_clear(UnwindOutput *output) {
  output->locs.clear();
  output->is_incomplete = true;
}
