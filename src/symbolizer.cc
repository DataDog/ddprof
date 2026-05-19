// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "symbolizer.hpp"

#include "ddog_profiling_utils.hpp" // for write_location_blaze
#include "ddprof_stats.hpp"
#include "ddres.hpp"
#include "demangler/demangler.hpp"
#include "logger.hpp"

#include <cassert>

namespace ddprof {
namespace {

std::string_view
demangled(const char *sym,
          ddprof::HeterogeneousLookupStringMap<std::string> &cache) {
  auto it = cache.find(sym);
  if (it == cache.end()) {
    it = cache
             .insert({std::string(sym),
                      ddprof::Demangler::non_microsoft_demangle(sym)})
             .first;
  }
  return it->second;
}

} // namespace

// static
DDRes Symbolizer::write_no_sym_cached(BlazeSymbolizerWrapper &wrapper,
                                      ElfAddress_t ip,
                                      ddog_prof_MappingId2 mapping_id,
                                      const ddog_prof_ProfilesDictionary *dict,
                                      std::span<ddog_prof_Location2> locations,
                                      unsigned &write_index) {
  if (write_index >= locations.size()) {
    return ddres_warn(DD_WHAT_UW_MAX_DEPTH);
  }
  auto cache_it = wrapper.nosym_cache.find(mapping_id);
  if (cache_it == wrapper.nosym_cache.end()) {
    const auto sopath = mapping_id ? get_string(dict, mapping_id->filename)
                                   : std::string_view{};
    ddog_prof_FunctionId2 fn = intern_function(dict, {}, sopath);
    if (!fn) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF,
                             "Unable to intern no-symbol function for %.*s",
                             static_cast<int>(sopath.size()), sopath.data());
    }
    cache_it = wrapper.nosym_cache.emplace(mapping_id, fn).first;
  }
  auto &loc = locations[write_index++];
  loc.mapping = mapping_id;
  loc.function = cache_it->second;
  loc.address = ip;
  loc.line = 0;
  return {};
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
    it->second.visited = true;
    return it->second;
  }
  // visited is set on BlazeSymbolizerWrapper construction
  auto [it, inserted] = _symbolizer_map.emplace(
      file_id, BlazeSymbolizerWrapper(elf_src, inlined_functions));
  DDPROF_DCHECK_FATAL(inserted, "Unable to insert symbolizer object");
  auto &symbolizer_wrapper = it->second;
  return symbolizer_wrapper;
}

DDRes Symbolizer::symbolize_pprof(std::span<ElfAddress_t> elf_addrs,
                                  FileInfoId_t file_id,
                                  const std::string &elf_src,
                                  ddog_prof_MappingId2 mapping_id,
                                  const ddog_prof_ProfilesDictionary *dict,
                                  std::span<ddog_prof_Location2> locations,
                                  unsigned &write_index,
                                  BlazeResultsWrapper &results) {
  if (elf_addrs.empty() || elf_src.empty()) {
    LG_WRN("Error in provided addresses when symbolizing pprofs");
    return ddres_warn(DD_WHAT_PPROF);
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
      // Invalidate caches: debug-derived symbols/lines are no longer valid.
      symbolizer_wrapper.function_id_cache.clear();
      symbolizer_wrapper.inlined_id_cache.clear();
      symbolizer_wrapper.address_cache.clear();
      blaze_res = blaze_symbolize_elf_virt_offsets(
          symbolizer_wrapper.symbolizer.get(), &src_elf, elf_addrs.data(),
          elf_addrs.size());
    }
    if (blaze_res) {
      DDPROF_DCHECK_FATAL(blaze_res->cnt == elf_addrs.size(),
                          "Symbolizer: Mismatch between size of returned "
                          "symbols and size of given elf addresses");
      results.blaze_results.push_back(blaze_res);
      for (size_t i = 0; i < blaze_res->cnt && i < elf_addrs.size(); ++i) {
        const blaze_sym *cur_sym = blaze_res->syms + i;
        if (cur_sym->addr == 0) {
          // Some binaries expose a single symbol at address 0 (ex:
          // DD_AGENT_V1). Avoid emitting it so the backend still attempts
          // symbolication.
          DDRES_CHECK_FWD(write_no_sym_cached(symbolizer_wrapper, elf_addrs[i],
                                              mapping_id, dict, locations,
                                              write_index));
          continue;
        }
        const ElfAddress_t addr = elf_addrs[i];

        // Level-2 hit: exact address seen before — write from caches, no blaze.
        auto addr_it = symbolizer_wrapper.address_cache.find(addr);
        if (addr_it != symbolizer_wrapper.address_cache.end()) {
          ddprof_stats_add(STATS_SYMBOLS_BLAZE_ADDR_HITS, 1, nullptr);
          const auto &entry = addr_it->second;
          const unsigned n = entry.lines.size();
          for (unsigned j = 0; j < n; ++j) {
            if (write_index >= locations.size()) {
              return ddres_warn(DD_WHAT_UW_MAX_DEPTH);
            }
            auto &loc = locations[write_index++];
            loc.mapping = mapping_id;
            loc.function = (j < n - 1)
                ? symbolizer_wrapper.inlined_id_cache.at({addr, j})
                : symbolizer_wrapper.function_id_cache.at(entry.func_start);
            loc.address = addr;
            loc.line = entry.lines[j];
          }
          continue;
        }

        // Cache miss for this address: run blaze, intern inlined frames, then
        // check function_id_cache[func_start] for the outer frame — saving
        // intern_function when the same function is reached from a new call
        // site.
        ddprof_stats_add(STATS_SYMBOLS_BLAZE_ADDR_MISSES, 1, nullptr);
        BlazeSymbolizerWrapper::AddressCacheEntry &new_entry =
            symbolizer_wrapper.address_cache[addr];
        new_entry.func_start = cur_sym->addr;

        constexpr std::string_view undef{};
        const auto sopath = mapping_id ? get_string(dict, mapping_id->filename)
                                       : std::string_view{};

        // Inlined frames (innermost first in blaze order, reversed for pprof)
        for (int k = cur_sym->inlined_cnt - 1;
             k >= 0 && write_index < kMaxStackDepth; --k) {
          const blaze_symbolize_inlined_fn *inlined = cur_sym->inlined + k;
          if (write_index >= locations.size()) {
            return ddres_warn(DD_WHAT_UW_MAX_DEPTH);
          }
          const std::string_view dname = inlined->name
              ? demangled(inlined->name, symbolizer_wrapper.demangled_names)
              : undef;
          const std::string_view fname = inlined->code_info.file
              ? std::string_view(inlined->code_info.file)
              : sopath;
          const auto inlined_idx =
              static_cast<unsigned>(cur_sym->inlined_cnt - 1 - k);
          const auto inlined_key = std::make_pair(addr, inlined_idx);
          auto fn_it = symbolizer_wrapper.inlined_id_cache.find(inlined_key);
          if (fn_it == symbolizer_wrapper.inlined_id_cache.end()) {
            ddprof_stats_add(STATS_SYMBOLS_BLAZE_INTERN_FN_CALLS, 1, nullptr);
            ddog_prof_FunctionId2 fn = intern_function(dict, dname, fname);
            if (!fn) {
              DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC,
                                     "OOM interning inlined function");
            }
            fn_it = symbolizer_wrapper.inlined_id_cache.emplace(inlined_key, fn)
                        .first;
          }
          auto &loc = locations[write_index++];
          loc.mapping = mapping_id;
          loc.function = fn_it->second;
          loc.address = addr;
          loc.line = inlined->code_info.line;
          new_entry.lines.push_back(inlined->code_info.line);
        }

        // Outer frame — shared across all call sites of the same function
        if (write_index >= locations.size()) {
          return ddres_warn(DD_WHAT_UW_MAX_DEPTH);
        }
        const std::string_view dname = cur_sym->name
            ? demangled(cur_sym->name, symbolizer_wrapper.demangled_names)
            : undef;
        const std::string_view fname = cur_sym->code_info.file
            ? std::string_view{cur_sym->code_info.file}
            : sopath;
        auto outer_it =
            symbolizer_wrapper.function_id_cache.find(cur_sym->addr);
        if (outer_it == symbolizer_wrapper.function_id_cache.end()) {
          ddprof_stats_add(STATS_SYMBOLS_BLAZE_INTERN_FN_CALLS, 1, nullptr);
          ddog_prof_FunctionId2 fn = intern_function(dict, dname, fname);
          if (!fn) {
            DDRES_RETURN_ERROR_LOG(DD_WHAT_BADALLOC, "OOM interning function");
          }
          outer_it =
              symbolizer_wrapper.function_id_cache.emplace(cur_sym->addr, fn)
                  .first;
        }
        auto &loc = locations[write_index++];
        loc.mapping = mapping_id;
        loc.function = outer_it->second;
        loc.address = addr;
        loc.line = cur_sym->code_info.line;
        new_entry.lines.push_back(cur_sym->code_info.line);
      }
      return {};
    }
  }

  // Handle the case of no blaze result
  // This can happen when file descriptors are exhausted
  // OR symbolization is disabled
  if (!_disable_symbolization) {
    auto &symbolizer_wrapper = get_symbolizer(file_id, elf_src);
    for (auto el : elf_addrs) {
      DDRES_CHECK_FWD(write_no_sym_cached(symbolizer_wrapper, el, mapping_id,
                                          dict, locations, write_index));
    }
  } else {
    for (auto el : elf_addrs) {
      if (write_index >= locations.size()) {
        return ddres_warn(DD_WHAT_UW_MAX_DEPTH);
      }
      DDRES_CHECK_FWD(write_location2_no_sym(el, mapping_id, dict,
                                             &locations[write_index++]));
    }
  }

  return {};
}
} // namespace ddprof
