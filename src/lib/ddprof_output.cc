#include "lib/ddprof_output.hpp"

extern "C" {
#include "ddprof_context.h"
#include "unwind_state.h"
}
#include "unwind_symbols.hpp"

namespace ddprof {

const ddprof::Symbol &get_symbol(const DDProfContext *ctx,
                                 const UnwindOutput *unwind_output,
                                 unsigned loc_idx) {
  const SymbolTable &symbol_table =
      ctx->worker_ctx.us->symbols_hdr->_symbol_table;
  return symbol_table[unwind_output->locs[loc_idx]._symbol_idx];
}

const ddprof::MapInfo &get_mapinfo(const DDProfContext *ctx,
                                   const UnwindOutput *unwind_output,
                                   unsigned loc_idx) {
  const MapInfoTable &mapinfo_table =
      ctx->worker_ctx.us->symbols_hdr->_mapinfo_table;
  return mapinfo_table[unwind_output->locs[loc_idx]._map_info_idx];
}

} // namespace ddprof
