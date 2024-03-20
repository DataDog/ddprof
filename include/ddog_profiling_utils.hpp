// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "datadog/blazesym.h"
#include "datadog/common.h"
#include "datadog/profiling.h"

#include "ddres.hpp"
#include "demangler/demangler.hpp"
#include "map_utils.hpp"
#include "mapinfo_table.hpp"
#include "symbol.hpp"
#include "unwind_output.hpp"

#include <string_view>
#include <vector>

namespace ddprof {
inline ddog_CharSlice to_CharSlice(std::string_view str) {
  return {.ptr = str.data(), .len = str.size()};
}

inline void write_function(const Symbol &symbol, ddog_prof_Function *ffi_func) {
  ffi_func->name = to_CharSlice(symbol._demangled_name);
  ffi_func->system_name = to_CharSlice(symbol._symname);
  ffi_func->filename = to_CharSlice(symbol._srcpath);
  // Not filled (Requires an extra location lookup using the symbol start range)
  ffi_func->start_line = 0;
}

inline void write_function(std::string_view demangled_name,
                           std::string_view file_name,
                           ddog_prof_Function *ffi_func) {
  ffi_func->name = to_CharSlice(demangled_name);
  ffi_func->system_name = {.ptr = nullptr, .len = 0};
  ffi_func->filename = to_CharSlice(file_name);
  // Not filled (Requires an extra location lookup using the symbol start range)
  ffi_func->start_line = 0;
}

inline void write_mapping(const MapInfo &mapinfo,
                          ddog_prof_Mapping *ffi_mapping) {
  ffi_mapping->memory_start = mapinfo._low_addr;
  ffi_mapping->memory_limit = mapinfo._high_addr;
  ffi_mapping->file_offset = mapinfo._offset;
  ffi_mapping->filename = to_CharSlice(mapinfo._sopath);
  ffi_mapping->build_id = to_CharSlice(mapinfo._build_id);
}

inline void write_location(const FunLoc &loc, const MapInfo &mapinfo,
                           const Symbol &symbol,
                           ddog_prof_Location *ffi_location,
                           bool use_process_adresses) {
  write_mapping(mapinfo, &ffi_location->mapping);
  write_function(symbol, &ffi_location->function);
  ffi_location->address = use_process_adresses ? loc.ip : loc.elf_addr;
  ffi_location->line = symbol._lineno;
}

// demangling caching based on stability of unordered map
// This will be moved to the backend
inline std::string_view get_or_insert_demangled_sym(
    const char *sym,
    ddprof::HeterogeneousLookupStringMap<std::string> &demangled_names) {
  auto it = demangled_names.find(sym);
  if (it == demangled_names.end()) {
    const std::string demangled_name =
        ddprof::Demangler::non_microsoft_demangle(sym);
    it = demangled_names.insert({std::string(sym), std::move(demangled_name)})
             .first;
  }
  return it->second;
}

inline DDRes write_location_blaze(
    ProcessAddress_t ip_or_elf_addr,
    ddprof::HeterogeneousLookupStringMap<std::string> &demangled_names,
    const MapInfo &mapinfo, const blaze_sym &blaze_sym, unsigned &cur_loc,
    std::span<ddog_prof_Location> locations_buff) {
  if (cur_loc >= locations_buff.size()) {
    return ddres_warn(DD_WHAT_UW_MAX_DEPTH);
  }
  constexpr std::string_view undef{};
  constexpr std::string_view undef_inlined = undef;
  for (unsigned i = 0; i < blaze_sym.inlined_cnt && cur_loc < kMaxStackDepth;
       ++i) {
    const blaze_symbolize_inlined_fn *inlined_fn = blaze_sym.inlined + i;
    ddog_prof_Location &ffi_location = locations_buff[cur_loc];
    write_mapping(mapinfo, &ffi_location.mapping);
    const std::string_view demangled_name = inlined_fn->name
        ? get_or_insert_demangled_sym(inlined_fn->name, demangled_names)
        : undef_inlined;
    write_function(demangled_name,
                   inlined_fn->code_info.file
                       ? std::string_view(inlined_fn->code_info.file)
                       : mapinfo._sopath,
                   &ffi_location.function);
    ffi_location.address = ip_or_elf_addr;
    ffi_location.line = inlined_fn->code_info.line;
    ++cur_loc;
  }

  if (cur_loc >= locations_buff.size()) {
    return ddres_warn(DD_WHAT_UW_MAX_DEPTH);
  }
  ddog_prof_Location &ffi_location = locations_buff[cur_loc];
  write_mapping(mapinfo, &ffi_location.mapping);
  const std::string_view demangled_name = blaze_sym.name
      ? get_or_insert_demangled_sym(blaze_sym.name, demangled_names)
      : undef;

  write_function(demangled_name,
                 blaze_sym.code_info.file
                     ? std::string_view{blaze_sym.code_info.file}
                     : std::string_view{mapinfo._sopath},
                 &ffi_location.function);
  ffi_location.address = ip_or_elf_addr;
  ffi_location.line = blaze_sym.code_info.line;
  ++cur_loc;
  return {};
}
} // namespace ddprof
