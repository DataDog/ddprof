// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "lib/ddprof_output.hpp"

extern "C" {
#include "ddprof_context.h"
#include "unwind_output.h"
}

#include "symbol_hdr.hpp"
#include "unwind_state.hpp"

namespace ddprof {

const Symbol &get_symbol(const DDProfContext *ctx,
                         const UnwindOutput *unwind_output, unsigned loc_idx) {
  const SymbolTable &symbol_table =
      ctx->worker_ctx.us->symbol_hdr._symbol_table;
  return symbol_table[unwind_output->locs[loc_idx]._symbol_idx];
}

const MapInfo &get_mapinfo(const DDProfContext *ctx,
                           const UnwindOutput *unwind_output,
                           unsigned loc_idx) {
  const MapInfoTable &mapinfo_table =
      ctx->worker_ctx.us->symbol_hdr._mapinfo_table;
  return mapinfo_table[unwind_output->locs[loc_idx]._map_info_idx];
}

} // namespace ddprof
