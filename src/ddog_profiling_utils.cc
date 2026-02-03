// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddog_profiling_utils.hpp"

#include "ddres.hpp"
#include "demangler/demangler.hpp"
#include "logger.hpp"

#include "datadog/blazesym.h"

namespace ddprof {
namespace {
// demangling caching based on stability of unordered map
// This will be moved to the backend
std::string_view get_or_insert_demangled_sym(
    const char *sym,
    ddprof::HeterogeneousLookupStringMap<std::string> &demangled_names) {
  auto it = demangled_names.find(sym);
  if (it == demangled_names.end()) {
    std::string demangled_name = ddprof::Demangler::non_microsoft_demangle(sym);
    it = demangled_names.insert({std::string(sym), std::move(demangled_name)})
             .first;
  }
  return it->second;
}
} // namespace

ddog_prof_StringId2 intern_string(const ddog_prof_ProfilesDictionary *dict,
                                  std::string_view str) {
  if (!dict || str.empty()) {
    return DDOG_PROF_STRINGID2_EMPTY;
  }
  ddog_prof_StringId2 string_id = nullptr;
  ddog_prof_Status status = ddog_prof_ProfilesDictionary_insert_str(
      &string_id, dict, to_CharSlice(str), DDOG_PROF_UTF8_OPTION_ASSUME);
  if (status.err != nullptr) {
    LG_WRN("Failed to intern string: %s", status.err);
    ddog_prof_Status_drop(&status);
    return DDOG_PROF_STRINGID2_EMPTY;
  }
  return string_id;
}

ddog_prof_FunctionId2
intern_function_ids(const ddog_prof_ProfilesDictionary *dict,
                    ddog_prof_StringId2 name_id, ddog_prof_StringId2 file_id,
                    ddog_prof_StringId2 system_name_id) {
  if (!dict) {
    return nullptr;
  }
  ddog_prof_Function2 function = {
      .name = name_id,
      .system_name = system_name_id,
      .file_name = file_id,
  };
  ddog_prof_FunctionId2 function_id = nullptr;
  ddog_prof_Status status =
      ddog_prof_ProfilesDictionary_insert_function(&function_id, dict,
                                                   &function);
  if (status.err != nullptr) {
    LG_WRN("Failed to intern function: %s", status.err);
    ddog_prof_Status_drop(&status);
    return nullptr;
  }
  return function_id;
}

ddog_prof_FunctionId2 intern_function(const ddog_prof_ProfilesDictionary *dict,
                                      std::string_view demangled_name,
                                      std::string_view file_name,
                                      std::string_view system_name) {
  ddog_prof_StringId2 name_id = intern_string(dict, demangled_name);
  ddog_prof_StringId2 file_id = intern_string(dict, file_name);
  ddog_prof_StringId2 system_name_id = intern_string(dict, system_name);
  return intern_function_ids(dict, name_id, file_id, system_name_id);
}

ddog_prof_MappingId2 intern_mapping(const ddog_prof_ProfilesDictionary *dict,
                                    const MapInfo &mapinfo) {
  if (!dict) {
    return nullptr;
  }
  ddog_prof_Mapping2 mapping = {
      .memory_start = mapinfo._low_addr,
      .memory_limit = mapinfo._high_addr,
      .file_offset = mapinfo._offset,
      .filename = intern_string(dict, mapinfo._sopath),
      .build_id = intern_string(dict, mapinfo._build_id),
  };
  ddog_prof_MappingId2 mapping_id = nullptr;
  ddog_prof_Status status =
      ddog_prof_ProfilesDictionary_insert_mapping(&mapping_id, dict, &mapping);
  if (status.err != nullptr) {
    LG_WRN("Failed to intern mapping: %s", status.err);
    ddog_prof_Status_drop(&status);
    return nullptr;
  }
  return mapping_id;
}

Symbol make_symbol(std::string symname, std::string demangled_name,
                   uint32_t lineno, std::string srcpath,
                   const ddog_prof_ProfilesDictionary *dict) {
  [[maybe_unused]] std::string ignored_symname = std::move(symname);
  ddog_prof_StringId2 name_id = intern_string(dict, demangled_name);
  ddog_prof_StringId2 file_id = intern_string(dict, srcpath);
  ddog_prof_FunctionId2 function_id =
      intern_function_ids(dict, name_id, file_id, DDOG_PROF_STRINGID2_EMPTY);
  return Symbol(name_id, file_id, lineno, function_id);
}

namespace {
ddog_CharSlice get_dict_string(const ddog_prof_ProfilesDictionary *dict,
                               ddog_prof_StringId2 string_id) {
  if (!dict || !string_id) {
    return {.ptr = nullptr, .len = 0};
  }
  ddog_CharSlice result{nullptr, 0};
  ddog_prof_Status status =
      ddog_prof_ProfilesDictionary_get_str(&result, dict, string_id);
  if (status.err != nullptr) {
    ddog_prof_Status_drop(&status);
    return {.ptr = nullptr, .len = 0};
  }
  return result;
}
} // namespace

void write_function(const Symbol &symbol,
                    const ddog_prof_ProfilesDictionary *dict,
                    ddog_prof_Function *ffi_func) {
  ffi_func->name = get_dict_string(dict, symbol._name_id);
  ffi_func->system_name = {.ptr = nullptr, .len = 0};
  ffi_func->filename = get_dict_string(dict, symbol._file_id);
}

void write_function(std::string_view demangled_name, std::string_view file_name,
                    ddog_prof_Function *ffi_func) {
  ffi_func->name = to_CharSlice(demangled_name);
  ffi_func->system_name = {.ptr = nullptr, .len = 0};
  ffi_func->filename = to_CharSlice(file_name);
}

void write_mapping(const MapInfo &mapinfo, ddog_prof_Mapping *ffi_mapping) {
  ffi_mapping->memory_start = mapinfo._low_addr;
  ffi_mapping->memory_limit = mapinfo._high_addr;
  ffi_mapping->file_offset = mapinfo._offset;
  ffi_mapping->filename = to_CharSlice(mapinfo._sopath);
  ffi_mapping->build_id = to_CharSlice(mapinfo._build_id);
}

void write_location(const FunLoc &loc, const MapInfo &mapinfo,
                    const Symbol &symbol,
                    const ddog_prof_ProfilesDictionary *dict,
                    ddog_prof_Location *ffi_location) {
  write_mapping(mapinfo, &ffi_location->mapping);
  write_function(symbol, dict, &ffi_location->function);
  ffi_location->address = loc.elf_addr;
  ffi_location->line = symbol._lineno;
}

void write_location(ProcessAddress_t ip_or_elf_addr,
                    std::string_view demangled_name, std::string_view file_name,
                    uint32_t lineno, const MapInfo &mapinfo,
                    ddog_prof_Location *ffi_location) {
  write_mapping(mapinfo, &ffi_location->mapping);
  write_function(demangled_name, file_name, &ffi_location->function);
  ffi_location->address = ip_or_elf_addr;
  ffi_location->line = lineno;
}

void write_location2(const FunLoc &loc, const MapInfo &mapinfo,
                     const Symbol &symbol, ddog_prof_Location2 *ffi_location) {
  ffi_location->mapping = mapinfo._mapping_id;
  ffi_location->function = symbol._function_id;
  ffi_location->address = loc.elf_addr;
  ffi_location->line = symbol._lineno;
}

DDRes write_location2_from_location(const ddog_prof_ProfilesDictionary *dict,
                                    const ddog_prof_Location &src,
                                    ddog_prof_Location2 *dst) {
  if (!dict) {
    return ddres_error(DD_WHAT_PPROF);
  }
  auto to_sv = [](ddog_CharSlice slice) -> std::string_view {
    if (!slice.ptr || slice.len == 0) {
      return {};
    }
    return {slice.ptr, slice.len};
  };
  const std::string_view fn_name = to_sv(src.function.name);
  const std::string_view fn_file = to_sv(src.function.filename);
  const std::string_view fn_system = to_sv(src.function.system_name);
  ddog_prof_FunctionId2 function_id =
      intern_function(dict, fn_name, fn_file, fn_system);
  if (function_id == nullptr) {
    return ddres_error(DD_WHAT_PPROF);
  }
  ddog_prof_Mapping2 mapping = {
      .memory_start = src.mapping.memory_start,
      .memory_limit = src.mapping.memory_limit,
      .file_offset = src.mapping.file_offset,
      .filename = intern_string(dict, to_sv(src.mapping.filename)),
      .build_id = intern_string(dict, to_sv(src.mapping.build_id)),
  };
  ddog_prof_MappingId2 mapping_id = nullptr;
  ddog_prof_Status status =
      ddog_prof_ProfilesDictionary_insert_mapping(&mapping_id, dict, &mapping);
  if (status.err != nullptr) {
    LG_WRN("Failed to intern mapping: %s", status.err);
    ddog_prof_Status_drop(&status);
    return ddres_error(DD_WHAT_PPROF);
  }
  dst->mapping = mapping_id;
  dst->function = function_id;
  dst->address = src.address;
  dst->line = src.line;
  return {};
}

DDRes write_location_blaze(
    ElfAddress_t elf_addr,
    ddprof::HeterogeneousLookupStringMap<std::string> &demangled_names,
    const MapInfo &mapinfo, const blaze_sym &blaze_sym, unsigned &cur_loc,
    std::span<ddog_prof_Location> locations_buff) {
  if (cur_loc >= locations_buff.size()) {
    return ddres_warn(DD_WHAT_UW_MAX_DEPTH);
  }
  constexpr std::string_view undef{};
  constexpr std::string_view undef_inlined = undef;
  for (int i = blaze_sym.inlined_cnt - 1; i >= 0 && cur_loc < kMaxStackDepth;
       --i) {
    const blaze_symbolize_inlined_fn *inlined_fn = blaze_sym.inlined + i;
    ddog_prof_Location &ffi_location = locations_buff[cur_loc];
    const std::string_view demangled_name = inlined_fn->name
        ? get_or_insert_demangled_sym(inlined_fn->name, demangled_names)
        : undef_inlined;
    write_location(elf_addr, demangled_name,
                   inlined_fn->code_info.file
                       ? std::string_view(inlined_fn->code_info.file)
                       : mapinfo._sopath,
                   inlined_fn->code_info.line, mapinfo, &ffi_location);
    ++cur_loc;
  }

  if (cur_loc >= locations_buff.size()) {
    return ddres_warn(DD_WHAT_UW_MAX_DEPTH);
  }
  ddog_prof_Location &ffi_location = locations_buff[cur_loc];

  const std::string_view demangled_name = blaze_sym.name
      ? get_or_insert_demangled_sym(blaze_sym.name, demangled_names)
      : undef;
  write_location(elf_addr, demangled_name,
                 blaze_sym.code_info.file
                     ? std::string_view{blaze_sym.code_info.file}
                     : std::string_view{mapinfo._sopath},
                 blaze_sym.code_info.line, mapinfo, &ffi_location);
  ++cur_loc;
  return {};
}
} // namespace ddprof
