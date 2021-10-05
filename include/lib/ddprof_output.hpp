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
