// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "unwind_output.hpp"

#include "mapinfo_table.hpp"
#include "symbol_table.hpp"

namespace ddprof {

struct DDProfContext DDProfContext;

const Symbol &get_symbol(const DDProfContext *ctx,
                         const UnwindOutput *unwind_output, unsigned loc_idx);

const MapInfo &get_mapinfo(const DDProfContext *ctx,
                           const UnwindOutput *unwind_output, unsigned loc_idx);

} // namespace ddprof
