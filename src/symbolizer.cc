// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "symbolizer.hpp"

#include "ddog_profiling_utils.hpp" // for write_location_blaze
#include "demangler/demangler.hpp"

#include <cassert>

namespace ddprof {
inline void write_error_function(ddog_prof_Function *ffi_func) {
  ffi_func->name = to_CharSlice("[no symbol]");
  ffi_func->start_line = 0;
}

inline void write_location_no_sym(ElfAddress_t ip, const MapInfo &mapinfo,
                                  ddog_prof_Location *ffi_location) {
  write_mapping(mapinfo, &ffi_location->mapping);
  write_error_function(&ffi_location->function);
  ffi_location->address = ip;
}

int Symbolizer::clear_unvisited() {
  // Remove all unvisited SymbolizerPack instances from the map (c++ 20)
  const auto count = std::erase_if(_symbolizer_map, [](const auto &item) {
    auto const &[key, symbolizer_pack] = item;
    return !symbolizer_pack._visited;
  });
  return count;
}

void Symbolizer::mark_unvisited() {
  // Reset visited flag for the remaining entries
  for (auto &item : _symbolizer_map) {
    item.second._visited = false;
  }
}

DDRes Symbolizer::symbolize_pprof(std::span<ElfAddress_t> addrs,
                                  FileInfoId_t file_id,
                                  const std::string &elf_src,
                                  const MapInfo &map_info,
                                  std::span<ddog_prof_Location> locations,
                                  unsigned &write_index,
                                  SessionResults &results) {
  blaze_symbolizer *symbolizer = nullptr;
  if (addrs.empty() || elf_src.empty()) {
    return ddres_warn(DD_WHAT_PPROF); // or some other error handling
  }
  ddprof::HeterogeneousLookupStringMap<std::string> *demangled_names = nullptr;
  const auto it = _symbolizer_map.find(file_id);
  const char *resovled_src = elf_src.c_str();
  // This is to avoid we change the path at every call (for different pids)
  // The cache takes into account the first path given
  if (it != _symbolizer_map.end()) {
    resovled_src = it->second._elf_src.c_str();
    symbolizer = it->second._symbolizer.get();
    it->second._visited = true;
    demangled_names = &(it->second._demangled_names);
  } else {
    auto pair = _symbolizer_map.emplace(file_id, SymbolizerWrapper(elf_src));
    assert(pair.second);
    if (!pair.second) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_SYMBOLIZER,
                             "Unable to create new symbolizer");
    }
    symbolizer = pair.first->second._symbolizer.get();
    demangled_names = &(pair.first->second._demangled_names);
  }
  const blaze_result *blaze_res = nullptr;
  if (!_disable_symbolization) {
    // Initialize the src_elf structure
    const blaze_symbolize_src_elf src_elf{
        .type_size = sizeof(blaze_symbolize_src_elf),
        .path = resovled_src,
        .debug_syms = true,
        .reserved = {},
    };

    // Symbolize the addresses
    blaze_res = blaze_symbolize_elf_virt_offsets(symbolizer, &src_elf,
                                                 addrs.data(), addrs.size());
    if (blaze_res) {
      assert(blaze_res->cnt == addrs.size());
      results.blaze_results.push_back(blaze_res);
      // Demangling caching based on stability of unordered map
      // This will be moved to the backend
      for (size_t i = 0; i < blaze_res->cnt; ++i) {
        const blaze_sym *cur_sym = blaze_res->syms + i;
        // Update the location
        // minor: address should be a process address
        DDRES_CHECK_FWD(write_location_blaze(addrs[i], (*demangled_names),
                                             map_info, cur_sym, write_index,
                                             locations));
      }
    }
  }
  // Handle the case of no blaze result
  // This can happen when file descriptors are exhausted
  // OR symbolization is disabled
  if (!blaze_res) {
    for (auto el : addrs) {
      write_location_no_sym(el, map_info, &locations[write_index++]);
    }
  }
  return {};
}
} // namespace ddprof