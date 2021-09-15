#pragma once

extern "C" {
#include "unwind_output.h"
}
#include "ipinfo_table.hpp"
#include "mapinfo_table.hpp"

typedef struct DDProfContext DDProfContext;
namespace ddprof {
const ddprof::IPInfo &get_ipinfo(const DDProfContext *ctx,
                                 const UnwindOutput *unwind_output,
                                 unsigned loc_idx);

const ddprof::MapInfo &get_mapinfo(const DDProfContext *ctx,
                                   const UnwindOutput *unwind_output,
                                   unsigned loc_idx);

} // namespace ddprof
