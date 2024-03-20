// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.

#include "unwind_state.hpp"

#include <array>
#include <datadog/blazesym.h>
#include <unistd.h>
#include <vector>

namespace ddprof {
// This is a test API. Use the symbolizer to populate pprof structures
std::vector<std::string> collect_symbols(UnwindState &state,
                                         blaze_symbolizer *symbolizer) {
  std::vector<std::string> demangled_symbols;
  auto &symbol_table = state.symbol_hdr._symbol_table;
  for (size_t iloc = 0; iloc < state.output.locs.size(); ++iloc) {
    std::string demangled_name;
    if (state.output.locs[iloc].symbol_idx == k_symbol_idx_null) {
      // Symbolize dynamically.
      std::array<ElfAddress_t, 1> elf_addr{state.output.locs[iloc].elf_addr};
      const auto &file_info_value = state.dso_hdr.get_file_info_value(
          state.output.locs[iloc].file_info_id);
      blaze_symbolize_src_elf src_elf{
          .type_size = sizeof(blaze_symbolize_src_elf),
          .path = file_info_value.get_path().c_str(),
          .debug_syms = true,
          .reserved = {},
      };
      const blaze_result *blaze_res = blaze_symbolize_elf_virt_offsets(
          symbolizer, &src_elf, elf_addr.data(), elf_addr.size());
      defer { blaze_result_free(blaze_res); };
      if (blaze_res && blaze_res->cnt >= 1 && blaze_res->syms[0].name) {
        demangled_name = blaze_res->syms[0].name;
      } else {
        demangled_name = "unknown";
      }
    } else {
      // Lookup the symbol from the symbol table.
      auto &symbol = symbol_table[state.output.locs[iloc].symbol_idx];
      demangled_name = symbol._demangled_name;
    }
    demangled_symbols.push_back(demangled_name);
  }
  return demangled_symbols;
}

} // namespace ddprof
