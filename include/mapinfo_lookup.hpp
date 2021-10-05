#pragma once

#include "ddprof_defs.h"
#include "mapinfo_table.hpp"

#include <string>
#include <unordered_map>

struct Dwfl_Module;

namespace ddprof {

typedef std::unordered_map<DsoUID_t, MapInfoIdx_t> DwflMapInfoLookup;

void mapinfo_lookup_get(DwflMapInfoLookup &mapinfo_map,
                        MapInfoTable &mapinfo_table, const Dwfl_Module *mod,
                        DsoUID_t dso_id, MapInfoIdx_t *map_info_idx);

} // namespace ddprof
