#include "lib/ddprof_output.hpp"

extern "C" {
#include "ddprof_context.h"
#include "unwind.h"
}
#include "unwind_symbols.hpp"

namespace ddprof {

const ddprof::IPInfo &get_ipinfo(const DDProfContext *ctx,
                                 const UnwindOutput *unwind_output,
                                 unsigned loc_idx) {
  const IPInfoTable &ipinfo_table =
      ctx->worker_ctx.us->symbols_hdr->_ipinfo_table;
  return ipinfo_table[unwind_output->locs[loc_idx]._ipinfo_idx];
}

const ddprof::MapInfo &get_mapinfo(const DDProfContext *ctx,
                                   const UnwindOutput *unwind_output,
                                   unsigned loc_idx) {
  const MapInfoTable &mapinfo_table =
      ctx->worker_ctx.us->symbols_hdr->_mapinfo_table;
  return mapinfo_table[unwind_output->locs[loc_idx]._map_info_idx];
}

} // namespace ddprof
