#pragma once

extern "C" {
#include "dwfl_internals.h"

#include "string_view.h"
}

#include <string>
#include <unordered_map>

namespace ddprof {

typedef std::unordered_map<GElf_Addr, std::string> mapinfo_hashmap;

void mapinfo_cache_get(mapinfo_hashmap &mapinfo_map, const Dwfl_Module *mod,
                       string_view *sname);

} // namespace ddprof
