// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres_def.hpp"
#include "map_utils.hpp"
#include "mapinfo_table.hpp"
#include "symbol.hpp"
#include "unwind_output.hpp"

#include "datadog/blazesym.h"
#include "datadog/common.h"
#include "datadog/profiling.h"

#include <string_view>

namespace ddprof {
inline ddog_CharSlice to_CharSlice(std::string_view str) {
  return {.ptr = str.data(), .len = str.size()};
}

void write_function(const Symbol &symbol, ddog_prof_Function *ffi_func);

void write_function(std::string_view demangled_name, std::string_view file_name,
                    ddog_prof_Function *ffi_func);

void write_mapping(const MapInfo &mapinfo, ddog_prof_Mapping *ffi_mapping);

void write_location(const FunLoc &loc, const MapInfo &mapinfo,
                    const Symbol &symbol, ddog_prof_Location *ffi_location,
                    bool use_process_adresses);

DDRes write_location_blaze(
    ProcessAddress_t ip_or_elf_addr,
    ddprof::HeterogeneousLookupStringMap<std::string> &demangled_names,
    const MapInfo &mapinfo, const blaze_sym &blaze_sym, unsigned &cur_loc,
    std::span<ddog_prof_Location> locations_buff);
} // namespace ddprof
