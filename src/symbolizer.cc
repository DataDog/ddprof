// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "symbolizer.hpp"

#include "ddog_profiling_utils.hpp" // for write_location_blaze
#include "ddres.hpp"
#include "demangler/demangler.hpp"
#include "logger.hpp"

#include <cassert>

namespace ddprof {
inline void write_location_no_sym(ElfAddress_t ip, const MapInfo &mapinfo,
                                  ddog_prof_Location *ffi_location) {
  write_mapping(mapinfo, &ffi_location->mapping);
  // write empty with empty function name, to enable remote symbolization
  write_function({}, mapinfo._sopath, &ffi_location->function);
  ffi_location->address = ip;
}

int Symbolizer::remove_unvisited() {
  // Remove all unvisited blaze_symbolizer instances from the map
  const auto count = std::erase_if(_symbolizer_map, [](const auto &item) {
    auto const &[key, blaze_symbolizer_wrapper] = item;
    return !blaze_symbolizer_wrapper.visited;
  });
  return count;
}

void Symbolizer::reset_unvisited_flag() {
  // Reset visited flag for the remaining entries
  for (auto &item : _symbolizer_map) {
    item.second.visited = false;
  }
}

Symbolizer::BlazeSymbolizerWrapper &
Symbolizer::get_symbolizer(FileInfoId_t file_id, const std::string &elf_src) {
  if (auto it = _symbolizer_map.find(file_id); it != _symbolizer_map.end()) {
    return it->second;
  }
  auto [it, inserted] = _symbolizer_map.emplace(
      file_id, BlazeSymbolizerWrapper(elf_src, inlined_functions));
  DDPROF_DCHECK_FATAL(inserted, "Unable to insert symbolizer object");
  auto &symbolizer_wrapper = it->second;
  symbolizer_wrapper.visited = true;
  return symbolizer_wrapper;
}

DDRes Symbolizer::symbolize_pprof(std::span<ElfAddress_t> elf_addrs,
                                  std::span<ProcessAddress_t> process_addrs,
                                  FileInfoId_t file_id,
                                  const std::string &elf_src,
                                  const MapInfo &map_info,
                                  std::span<ddog_prof_Location> locations,
                                  unsigned &write_index,
                                  BlazeResultsWrapper &results) {
  if (elf_addrs.size() != process_addrs.size() || elf_addrs.empty() ||
      elf_src.empty()) {
    LG_WRN("Error in provided addresses when symbolizing pprofs");
    return ddres_warn(DD_WHAT_PPROF); // or some other error handling
  }

  if (!_disable_symbolization) {
    auto &symbolizer_wrapper = get_symbolizer(file_id, elf_src);

    blaze_symbolize_src_elf src_elf{
        .type_size = sizeof(blaze_symbolize_src_elf),
        .path = symbolizer_wrapper.elf_src.c_str(),
        .debug_syms = symbolizer_wrapper.use_debug,
        .reserved = {},
    };

    // Symbolize the addresses
    const auto *blaze_res = blaze_symbolize_elf_virt_offsets(
        symbolizer_wrapper.symbolizer.get(), &src_elf, elf_addrs.data(),
        elf_addrs.size());
    if (!blaze_res && symbolizer_wrapper.use_debug) {
      // Symbolization failed, retry without using debug symbols
      // blazesym curently does not support compressed debug sections:
      // cf. https://github.com/libbpf/blazesym/issues/581
      LG_NTC("Unable to symbolize with debug symbols, retrying for %s (%s)",
             elf_src.c_str(), blaze_err_str(blaze_err_last()));
      symbolizer_wrapper.use_debug = false;
      src_elf.debug_syms = false;
      blaze_res = blaze_symbolize_elf_virt_offsets(
          symbolizer_wrapper.symbolizer.get(), &src_elf, elf_addrs.data(),
          elf_addrs.size());
    }
    if (blaze_res) {
      DDPROF_DCHECK_FATAL(blaze_res->cnt == elf_addrs.size(),
                          "Symbolizer: Mismatch between size of returned "
                          "symbols and size of given elf addresses");
      results.blaze_results.push_back(blaze_res);
      // Demangling cache based on stability of unordered map
      // This will be moved to the backend
      for (size_t i = 0; i < blaze_res->cnt && i < elf_addrs.size(); ++i) {
        const blaze_sym *cur_sym = blaze_res->syms + i;
        // Update the location
        DDRES_CHECK_FWD(write_location_blaze(
            _reported_addr_format == k_elf ? elf_addrs[i] : process_addrs[i],
            symbolizer_wrapper.demangled_names, map_info, *cur_sym, write_index,
            locations));
      }
      return {};
    }
  }

  // Handle the case of no blaze result
  // This can happen when file descriptors are exhausted
  // OR symbolization is disabled
  for (auto el : (_reported_addr_format == k_elf) ? elf_addrs : process_addrs) {
    write_location_no_sym(el, map_info, &locations[write_index++]);
  }

  return {};
}
} // namespace ddprof
