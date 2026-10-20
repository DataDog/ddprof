// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddog_profiling_utils.hpp"
#include "ddres.hpp"
#include "symbol_hdr.hpp"
#include "symbolizer.hpp"

#include <array>
#include <dlfcn.h>
#include <gtest/gtest.h>

extern "C" int symbolizer_source_fixture(int);

namespace ddprof {

TEST(Symbolizer, source_files_within_one_function) {
  Dl_info info{};
  ASSERT_NE(dladdr(reinterpret_cast<void *>(&symbolizer_source_fixture), &info),
            0);
  const auto bias = reinterpret_cast<ElfAddress_t>(info.dli_fbase);
  const auto *first_address = dlsym(RTLD_DEFAULT, "symbolizer_source_first");
  const auto *second_address = dlsym(RTLD_DEFAULT, "symbolizer_source_second");
  ASSERT_NE(first_address, nullptr);
  ASSERT_NE(second_address, nullptr);
  const auto first = reinterpret_cast<ElfAddress_t>(first_address) - bias;
  const auto second = reinterpret_cast<ElfAddress_t>(second_address) - bias;
  ASSERT_NE(first, second);

  SymbolHdr symbols;
  const auto *dict = symbols.profiles_dictionary();
  const auto mapping = intern_mapping(dict, 0, 0, 0, info.dli_fname, {});
  ASSERT_NE(mapping, nullptr);
  Symbolizer symbolizer(true);
  std::array<ElfAddress_t, 4> addresses{first, second, first, second};
  std::array<ddog_prof_Location2, 4> locations{};
  unsigned written = 0;
  std::array<uint32_t, 2> expected_lines{};
  {
    Symbolizer::BlazeResultsWrapper results;
    ASSERT_TRUE(IsDDResOK(
        symbolizer.symbolize_pprof(addresses, 1, info.dli_fname, mapping, dict,
                                   locations, written, results)));
    ASSERT_EQ(written, locations.size());
    ASSERT_EQ(results.blaze_results.size(), 1u);
    ASSERT_EQ(results.blaze_results[0]->syms[0].inlined_cnt, 0u);
    ASSERT_EQ(results.blaze_results[0]->syms[1].inlined_cnt, 0u);
    ASSERT_EQ(results.blaze_results[0]->syms[0].addr,
              results.blaze_results[0]->syms[1].addr);
    expected_lines = {results.blaze_results[0]->syms[0].code_info.line,
                      results.blaze_results[0]->syms[1].code_info.line};
  }

  for (unsigned i = 0; i < locations.size(); ++i) {
    ASSERT_NE(locations[i].function, nullptr);
    EXPECT_EQ(get_string(dict, locations[i].function->file_name),
              i % 2 == 0 ? "symbolizer-source-first.c"
                         : "symbolizer-source-second.c");
    EXPECT_EQ(locations[i].line, expected_lines[i % 2]);
  }
  EXPECT_NE(locations[0].function, locations[1].function);
  EXPECT_EQ(locations[0].function, locations[2].function);
  EXPECT_EQ(locations[1].function, locations[3].function);
  const auto stats = symbolizer.get_and_reset_blaze_stats();
  EXPECT_EQ(stats.addr_misses, 2u);
  EXPECT_EQ(stats.addr_hits, 2u);
  EXPECT_EQ(stats.intern_fn_calls, 2u);

  written = 0;
  Symbolizer::BlazeResultsWrapper results;
  ASSERT_TRUE(IsDDResOK(symbolizer.symbolize_pprof(addresses, 1, info.dli_fname,
                                                   mapping, dict, locations,
                                                   written, results)));
  ASSERT_EQ(written, locations.size());
  EXPECT_EQ(get_string(dict, locations[1].function->file_name),
            "symbolizer-source-second.c");
  EXPECT_EQ(symbolizer.get_and_reset_blaze_stats().intern_fn_calls, 0u);
}

} // namespace ddprof
