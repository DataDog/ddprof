#pragma once

extern "C" {
#include "string_view.h"
}
#include "ddprof_defs.h"
#include "mapinfo_table.hpp"

#include <string>
#include <unordered_map>

struct Dwfl_Module;

namespace ddprof {

typedef std::unordered_map<ElfAddress_t, MapInfoIdx_t> MapInfoLookup;

void mapinfo_lookup_get(MapInfoLookup &mapinfo_map, MapInfoTable &mapinfo_table,
                        const Dwfl_Module *mod, MapInfoIdx_t *map_info_idx);

} // namespace ddprof
