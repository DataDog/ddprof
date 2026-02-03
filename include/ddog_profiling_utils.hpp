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

#include "datadog/common.h"
#include "datadog/profiling.h"

#include <string_view>

struct blaze_sym;

namespace ddprof {
inline ddog_CharSlice to_CharSlice(std::string_view str) {
  return {.ptr = str.data(), .len = str.size()};
}

ddog_prof_StringId2 intern_string(const ddog_prof_ProfilesDictionary *dict,
                                  std::string_view str);

std::string_view get_string(const ddog_prof_ProfilesDictionary *dict,
                            ddog_prof_StringId2 string_id);

std::string_view get_location2_function_name(
    const ddog_prof_ProfilesDictionary *dict, const ddog_prof_Location2 &loc);

std::string_view get_location2_mapping_filename(
    const ddog_prof_ProfilesDictionary *dict, const ddog_prof_Location2 &loc);

ddog_prof_FunctionId2
intern_function_ids(const ddog_prof_ProfilesDictionary *dict,
                    ddog_prof_StringId2 name_id, ddog_prof_StringId2 file_id,
                    ddog_prof_StringId2 system_name_id);

ddog_prof_FunctionId2 intern_function(const ddog_prof_ProfilesDictionary *dict,
                                      std::string_view demangled_name,
                                      std::string_view file_name,
                                      std::string_view system_name = {});

ddog_prof_MappingId2 intern_mapping(const ddog_prof_ProfilesDictionary *dict,
                                    const MapInfo &mapinfo);

Symbol make_symbol(std::string symname, std::string demangled_name,
                   uint32_t lineno, std::string srcpath,
                   const ddog_prof_ProfilesDictionary *dict);

void write_function(const Symbol &symbol,
                    const ddog_prof_ProfilesDictionary *dict,
                    ddog_prof_Function *ffi_func);

void write_function(std::string_view demangled_name, std::string_view file_name,
                    ddog_prof_Function *ffi_func);

void write_mapping(const MapInfo &mapinfo, ddog_prof_Mapping *ffi_mapping);

void write_location(const FunLoc &loc, const MapInfo &mapinfo,
                    const Symbol &symbol,
                    const ddog_prof_ProfilesDictionary *dict,
                    ddog_prof_Location *ffi_location);

void write_location(ProcessAddress_t ip_or_elf_addr,
                    std::string_view demangled_name, std::string_view file_name,
                    uint32_t lineno, const MapInfo &mapinfo,
                    ddog_prof_Location *ffi_location);

void write_location2(const FunLoc &loc, const MapInfo &mapinfo,
                     const Symbol &symbol, ddog_prof_Location2 *ffi_location);

DDRes write_location2_from_location(const ddog_prof_ProfilesDictionary *dict,
                                    const ddog_prof_Location &src,
                                    ddog_prof_Location2 *dst);

DDRes write_location_blaze(
    ElfAddress_t elf_addr,
    ddprof::HeterogeneousLookupStringMap<std::string> &demangled_names,
    const MapInfo &mapinfo, const blaze_sym &blaze_sym, unsigned &cur_loc,
    std::span<ddog_prof_Location> locations_buff);
} // namespace ddprof
