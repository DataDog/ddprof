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

Symbolizer::Symbolizer() {
  // todo : pass sym options
  constexpr blaze_symbolizer_opts opts{
      .type_size = sizeof(blaze_symbolizer_opts),
      .auto_reload = false,
      .code_info = false,
      .inlined_fns = false,
      .demangle = false,
      .reserved = {}
  };
  _symbolizer = blaze_symbolizer_new_opts(&opts);
}

DDRes Symbolizer::symbolize(const std::span<ElfAddress_t> addrs,
                            const std::string &elf_src, const MapInfo &map_info,
                            std::span<ddog_prof_Location> locations,
                            unsigned &write_index, SessionResults &results) {
  if (addrs.empty() || elf_src.empty()) {
    return ddres_warn(DD_WHAT_PPROF); // or some other error handling
  }

  // Initialize the src_elf structure
  blaze_symbolize_src_elf src_elf{
      .type_size = sizeof(blaze_symbolize_src_elf),
      .path = elf_src.c_str(),
      .debug_syms = true,
      .reserved = {},
  };

  // Symbolize the addresses
  const blaze_result *blaze_res = blaze_symbolize_elf_virt_offsets(
      _symbolizer, &src_elf, addrs.data(), addrs.size());

  if (blaze_res) {
    assert(blaze_res->cnt == addrs.size());
    results.blaze_results.push_back(blaze_res);
    // demangling caching based on stability of unordered map
    // This will be moved to the backend
    for (size_t i = 0; i < blaze_res->cnt; ++i) {
      const blaze_sym *cur_sym = blaze_res->syms + i;
      // Update the location
      // minor: address should be a process address
      DDRES_CHECK_FWD(write_location_blaze(addrs[i], _demangled_names, map_info,
                                           cur_sym, write_index, locations));
    }
  } else {
    // Handle the case of no blaze result
    // This can happen when file descriptors are exhausted
    LG_DBG("No blaze result for %s", elf_src.data());
    for (size_t i = 0; i < addrs.size(); ++i) {
      // Update the location as is
      write_location_no_sym(addrs[i], map_info, &locations[write_index++]);
    }
  }
  return {};
}

Symbolizer::~Symbolizer() { blaze_symbolizer_free(_symbolizer); }

} // namespace ddprof
