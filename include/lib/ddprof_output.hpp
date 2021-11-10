// Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

#pragma once

extern "C" {
#include "unwind_output.h"
}
#include "mapinfo_table.hpp"
#include "symbol_table.hpp"

typedef struct DDProfContext DDProfContext;
namespace ddprof {
const ddprof::Symbol &get_symbol(const DDProfContext *ctx,
                                 const UnwindOutput *unwind_output,
                                 unsigned loc_idx);

const ddprof::MapInfo &get_mapinfo(const DDProfContext *ctx,
                                   const UnwindOutput *unwind_output,
                                   unsigned loc_idx);

} // namespace ddprof
