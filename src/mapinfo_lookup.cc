#include "mapinfo_lookup.hpp"

extern "C" {
#include "dwfl_internals.h"
#include <dwarf.h>
}

#include "ddres.h"

namespace ddprof {

void mapinfo_lookup_get(DwflMapInfoLookup &mapinfo_map,
                        MapInfoTable &mapinfo_table, const Dwfl_Module *mod,
                        DsoUID_t dso_id, MapInfoIdx_t *map_info_idx) {

  auto const it = mapinfo_map.find(dso_id);
  if (it != mapinfo_map.end()) {
    *map_info_idx = it->second;
  } else {
    char *localsname = strrchr(mod->name, '/');
    std::string sname_str(localsname ? localsname + 1 : mod->name);
    *map_info_idx = mapinfo_table.size();

    mapinfo_table.emplace_back(mod->low_addr, mod->high_addr,
                               std::move(sname_str));
    mapinfo_map.insert(std::make_pair<ElfAddress_t, MapInfoIdx_t>(
        std::move(dso_id), MapInfoIdx_t(*map_info_idx)));
  }
}

} // namespace ddprof
