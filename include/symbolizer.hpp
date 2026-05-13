// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include "datadog/blazesym.h"
#include "ddprof_defs.hpp"
#include "ddprof_file_info-i.hpp"
#include "ddres_def.hpp"
#include "map_utils.hpp"
#include "mapinfo_table.hpp"
#include "symbol.hpp"
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

struct ddog_prof_Location2;
struct ddog_prof_ProfilesDictionary;

namespace ddprof {
class Symbolizer {
public:
  explicit Symbolizer(bool inlined_functions = false,
                      bool disable_symbolization = false)
      : inlined_functions(inlined_functions),
        _disable_symbolization(disable_symbolization) {}

  struct BlazeResultsWrapper {
    BlazeResultsWrapper() = default;
    ~BlazeResultsWrapper() {
      for (auto &result : blaze_results) {
        blaze_syms_free(result);
      }
    }

    // Delete copy constructor and copy assignment operator
    BlazeResultsWrapper(const BlazeResultsWrapper &) = delete;
    BlazeResultsWrapper &operator=(const BlazeResultsWrapper &) = delete;

    // Optionally define move constructor and move assignment operator
    BlazeResultsWrapper(BlazeResultsWrapper &&other) noexcept
        : blaze_results(std::move(other.blaze_results)) {
      other.blaze_results.clear();
    }

    BlazeResultsWrapper &operator=(BlazeResultsWrapper &&other) noexcept {
      if (this != &other) {
        for (auto &result : blaze_results) {
          blaze_syms_free(result);
        }
        blaze_results = std::move(other.blaze_results);
        other.blaze_results.clear();
      }
      return *this;
    }

    std::vector<const blaze_syms *> blaze_results;
  };

  /// Fills the locations at the write index using address and elf source.
  /// assumption is that all addresses are from this source file
  /// Parameters
  /// addrs - Elf address
  /// file_id - a way to identify this file in a unique way
  /// elf_src - a path to the source file (idealy stable)
  /// mapping_id - interned mapping handle for this ELF file
  /// dict - the profiling dictionary for string interning
  /// locations - the output pprof structure (Location2 with interned IDs)
  /// write_index - input / output parameter updated based on what is written
  /// results - A handle object for lifetime of strings.
  ///          Should be kept until interned strings are no longer needed.
  DDRes symbolize_pprof(std::span<ElfAddress_t> addrs, FileInfoId_t file_id,
                        const std::string &elf_src,
                        ddog_prof_MappingId2 mapping_id,
                        const ddog_prof_ProfilesDictionary *dict,
                        std::span<ddog_prof_Location2> locations,
                        unsigned &write_index, BlazeResultsWrapper &results);
  int remove_unvisited();
  void reset_unvisited_flag();

private:
  struct BlazeSymbolizerDeleter {
    void operator()(blaze_symbolizer *ptr) const {
      if (ptr != nullptr) {
        blaze_symbolizer_free(ptr);
      }
    }
  };

  struct BlazeSymbolizerWrapper {
    static blaze_symbolizer_opts create_opts(bool inlined_fns) {
      return blaze_symbolizer_opts{.type_size = sizeof(blaze_symbolizer_opts),
                                   .auto_reload = false,
                                   .code_info = true,
                                   .inlined_fns = inlined_fns,
                                   .demangle = false,
                                   .reserved = {}};
    }
    explicit BlazeSymbolizerWrapper(std::string elf_src, bool inlined_fns)
        : opts(create_opts(inlined_fns)),
          symbolizer(blaze_symbolizer_new_opts(&opts)),
          elf_src(std::move(elf_src)), use_debug(inlined_fns) {}

    // Two-level cache to avoid re-interning FunctionId2 handles on every
    // sample.
    //
    // Level 1 — function identity (shared across all call sites of same
    // function):
    //   func_start_addr → FunctionId2  (outer frames, keyed by blaze_sym.addr)
    //   {elf_addr, inlined_idx}        → FunctionId2  (inlined frames)
    //
    // Level 2 — per call-site (fast full hit when we've seen this exact
    // address):
    //   elf_addr → { func_start, lines[] }
    //
    // On a cache miss for a new elf_addr, we still run blaze but check
    // function_id_cache[func_start] for the outer frame — saving
    // intern_function calls when the same function is reached from multiple
    // call sites.

    struct AddressCacheEntry {
      ElfAddress_t func_start;     // blaze_sym.addr (function start address)
      std::vector<uint32_t> lines; // line per frame: inlined first, outer last
    };

    // Pair hash for the inlined_id_cache.
    // In theory a pair<ElfAddress_t, unsigned> could also be packed into a
    // uint64_t (ELF vaddrs are well under 48 bits on both aarch64 and x86_64
    // in practice), but using std::pair avoids any architectural assumption.
    struct InlinedKeyHash {
      std::size_t operator()(const std::pair<ElfAddress_t, unsigned> &p) const {
        // Boost-style hash_combine: golden-ratio constant for avalanche mixing.
        static constexpr std::size_t kGoldenRatio = 0x9E3779B9U;
        static constexpr unsigned kShiftLeft = 6;
        static constexpr unsigned kShiftRight = 2;
        std::size_t h = std::hash<ElfAddress_t>{}(p.first);
        h ^= std::hash<unsigned>{}(p.second) + kGoldenRatio +
             (h << kShiftLeft) + (h >> kShiftRight);
        return h;
      }
    };

    blaze_symbolizer_opts opts;
    std::unique_ptr<blaze_symbolizer, BlazeSymbolizerDeleter> symbolizer;
    ddprof::HeterogeneousLookupStringMap<std::string> demangled_names;
    // func_start → FunctionId2 (outer frames, shared across all call sites)
    std::unordered_map<ElfAddress_t, ddog_prof_FunctionId2> function_id_cache;
    // {elf_addr, inlined_idx} → FunctionId2 (inlined frames, per call site)
    std::unordered_map<std::pair<ElfAddress_t, unsigned>, ddog_prof_FunctionId2,
                       InlinedKeyHash>
        inlined_id_cache;
    std::unordered_map<ElfAddress_t, AddressCacheEntry> address_cache;
    // mapping_id → no-sym FunctionId2 (intern_function("", sopath) result)
    std::unordered_map<ddog_prof_MappingId2, ddog_prof_FunctionId2> nosym_cache;
    std::string elf_src;
    bool visited{true};
    bool use_debug;
  };

  BlazeSymbolizerWrapper &get_symbolizer(FileInfoId_t file_id,
                                         const std::string &elf_src);

  static DDRes write_no_sym_cached(BlazeSymbolizerWrapper &wrapper,
                                   ElfAddress_t ip,
                                   ddog_prof_MappingId2 mapping_id,
                                   const ddog_prof_ProfilesDictionary *dict,
                                   std::span<ddog_prof_Location2> locations,
                                   unsigned &write_index);

  std::unordered_map<FileInfoId_t, BlazeSymbolizerWrapper> _symbolizer_map;
  bool inlined_functions;
  bool _disable_symbolization;
};
} // namespace ddprof
