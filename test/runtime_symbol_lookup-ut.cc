// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "ddog_profiling_utils.hpp"
#include "loghandle.hpp"
#include "runtime_symbol_lookup.hpp"
#include "symbol_hdr.hpp"
#include "symbol_table.hpp"

#include <cstdio>
#include <datadog/profiling.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace ddprof {

namespace {
std::string dict_string(const ddog_prof_ProfilesDictionary *dict,
                        ddog_prof_StringId2 string_id) {
  if (!dict || !string_id) {
    return {};
  }
  ddog_CharSlice slice{nullptr, 0};
  ddog_prof_Status status =
      ddog_prof_ProfilesDictionary_get_str(&slice, dict, string_id);
  if (status.err != nullptr) {
    ddog_prof_Status_drop(&status);
    return {};
  }
  return std::string(slice.ptr, slice.len);
}
} // namespace

TEST(runtime_symbol_lookup, dictionary_reuses_string_ids) {
  SymbolHdr symbol_hdr;
  const ddog_prof_ProfilesDictionary *dict = symbol_hdr.profiles_dictionary();

  ddog_prof_StringId2 first = intern_string(dict, "jit-symbol");
  ddog_prof_StringId2 second = intern_string(dict, "jit-symbol");

  EXPECT_EQ(first, second);
  EXPECT_EQ(dict_string(dict, first), "jit-symbol");
}

TEST(runtime_symbol_lookup, no_map) {
  SymbolHdr symbol_hdr;
  SymbolTable symbol_table;
  RuntimeSymbolLookup runtime_symbol_lookup(UNIT_TEST_DATA);
  ProcessAddress_t pc = 0x7FB0614BB980;
  // no pid 43
  SymbolIdx_t symbol_idx = runtime_symbol_lookup.get_or_insert(
      43, pc, symbol_table, symbol_hdr.profiles_dictionary());
  // We expect no symbols to be found for this pid
  ASSERT_EQ(symbol_idx, -1);
}

TEST(runtime_symbol_lookup, parse_map) {
  SymbolHdr symbol_hdr;
  SymbolTable symbol_table;
  RuntimeSymbolLookup runtime_symbol_lookup(UNIT_TEST_DATA);
  // reads a file with symbols generated from .NET
  ProcessAddress_t pc = 0x7FB0614BB980;
  SymbolIdx_t symbol_idx = runtime_symbol_lookup.get_or_insert(
      42, pc, symbol_table, symbol_hdr.profiles_dictionary());
  ASSERT_NE(symbol_idx, -1);
  const std::string sym_name =
      dict_string(symbol_hdr.profiles_dictionary(),
                  symbol_table[symbol_idx]._function_id->name);
  ASSERT_TRUE(sym_name.find("RuntimeEnvironmentInfo::get_OsPlatform") !=
              std::string::npos);
}

TEST(runtime_symbol_lookup, overflow) {
  SymbolHdr symbol_hdr;
  SymbolTable symbol_table;
  RuntimeSymbolLookup runtime_symbol_lookup(UNIT_TEST_DATA);
  // reads a file with symbols generated from .NET
  {
    ProcessAddress_t pc = 0x00007FB06149E6A0;
    SymbolIdx_t symbol_idx = runtime_symbol_lookup.get_or_insert(
        1, pc, symbol_table, symbol_hdr.profiles_dictionary());
    ASSERT_NE(symbol_idx, -1);
    const std::string sym_name =
        dict_string(symbol_hdr.profiles_dictionary(),
                    symbol_table[symbol_idx]._function_id->name);
    LG_NFO("%s", sym_name.c_str());
    ASSERT_TRUE(sym_name.size() <= 300);
  }
  {
    ProcessAddress_t pc = 0xFFFFFFFFFFFFFFFE;
    SymbolIdx_t symbol_idx = runtime_symbol_lookup.get_or_insert(
        1, pc, symbol_table, symbol_hdr.profiles_dictionary());
    ASSERT_EQ(symbol_idx, -1);
  }
}

TEST(runtime_symbol_lookup, jitdump_simple) {
  SymbolHdr symbol_hdr;
  pid_t mypid = getpid();
  SymbolTable symbol_table;
  RuntimeSymbolLookup runtime_symbol_lookup("");
  ProcessAddress_t pc = 0x7bea23b00390;
  std::string jit_path =
      std::string(UNIT_TEST_DATA) + "/" + "jit-simple-julia.dump";
  SymbolIdx_t symbol_idx = runtime_symbol_lookup.get_or_insert_jitdump(
      mypid, pc, symbol_table, symbol_hdr.profiles_dictionary(), jit_path);
  ASSERT_NE(symbol_idx, -1);
  const std::string sym_name =
      dict_string(symbol_hdr.profiles_dictionary(),
                  symbol_table[symbol_idx]._function_id->name);
  ASSERT_EQ(std::string("julia_b_11"), sym_name);
}

TEST(runtime_symbol_lookup, double_load) {
  SymbolHdr symbol_hdr;
  // ensure we don't increase number of symbols when we load several times
  pid_t mypid = getpid();
  SymbolTable symbol_table;
  RuntimeSymbolLookup runtime_symbol_lookup("");
  ProcessAddress_t pc = 0xbadbeef;
  std::string jit_path =
      std::string(UNIT_TEST_DATA) + "/" + "jit-simple-julia.dump";
  SymbolIdx_t symbol_idx = runtime_symbol_lookup.get_or_insert_jitdump(
      mypid, pc, symbol_table, symbol_hdr.profiles_dictionary(), jit_path);
  ASSERT_EQ(symbol_idx, -1);
  auto current_table_size = symbol_table.size();
  symbol_idx = runtime_symbol_lookup.get_or_insert_jitdump(
      mypid, pc, symbol_table, symbol_hdr.profiles_dictionary(), jit_path);
  auto new_table_size = symbol_table.size();
  // Check that we did not grow in number of symbols (as they are the same)
  ASSERT_EQ(current_table_size, new_table_size);
}

TEST(runtime_symbol_lookup, jitdump_partial) {
  SymbolHdr symbol_hdr;
  // Test what happens when the file is altered
  pid_t mypid = getpid();
  SymbolTable symbol_table;
  RuntimeSymbolLookup runtime_symbol_lookup("");
  ProcessAddress_t pc = 0xbadbeef;
  {
    std::string jit_path =
        std::string(UNIT_TEST_DATA) + "/" + "jit-julia-partial.dump";
    SymbolIdx_t symbol_idx = runtime_symbol_lookup.get_or_insert_jitdump(
        mypid, pc, symbol_table, symbol_hdr.profiles_dictionary(), jit_path);
    ASSERT_EQ(symbol_idx, -1);
  }
  {
    std::string jit_path =
        std::string(UNIT_TEST_DATA) + "/" + "jit-dotnet-partial.dump";
    SymbolIdx_t symbol_idx = runtime_symbol_lookup.get_or_insert_jitdump(
        mypid, pc, symbol_table, symbol_hdr.profiles_dictionary(), jit_path);
    ASSERT_EQ(symbol_idx, -1);
    ASSERT_NE(symbol_table.size(), 0);
  }
}

TEST(runtime_symbol_lookup, jitdump_bad_file) {
  SymbolHdr symbol_hdr;
  pid_t mypid = getpid();
  SymbolTable symbol_table;
  RuntimeSymbolLookup runtime_symbol_lookup("");
  ProcessAddress_t pc = 0xbadbeef;
  std::string jit_path = std::string(UNIT_TEST_DATA) + "/" + "bad_file.dump";
  SymbolIdx_t symbol_idx = runtime_symbol_lookup.get_or_insert_jitdump(
      mypid, pc, symbol_table, symbol_hdr.profiles_dictionary(), jit_path);
  ASSERT_EQ(symbol_idx, -1);

  // this should not trigger another read
  symbol_idx = runtime_symbol_lookup.get_or_insert_jitdump(
      mypid, pc, symbol_table, symbol_hdr.profiles_dictionary(), jit_path);
  ASSERT_EQ(symbol_idx, -1);
}

TEST(runtime_symbol_lookup, relative_path) {
  SymbolHdr symbol_hdr;
  std::string jit_path =
      std::string(".debug/jit/llvm-something/jit-1560413.dump");
  // specify a fake /proc directory
  RuntimeSymbolLookup runtime_symbol_lookup(UNIT_TEST_DATA);
  ProcessAddress_t pc = 0x7e1304b00a30;
  SymbolTable symbol_table;
  SymbolIdx_t symbol_idx = runtime_symbol_lookup.get_or_insert_jitdump(
      42, pc, symbol_table, symbol_hdr.profiles_dictionary(), jit_path);
  ASSERT_NE(symbol_idx, -1);
  const std::string sym_name =
      dict_string(symbol_hdr.profiles_dictionary(),
                  symbol_table[symbol_idx]._function_id->name);
  EXPECT_EQ(sym_name, "julia_b_11");
  {
    RuntimeSymbolLookup::Stats stats = runtime_symbol_lookup.get_stats();
    EXPECT_EQ(stats._symbol_count, 20);
  }
}

TEST(runtime_symbol_lookup, jitdump_vs_perfmap) {
  SymbolHdr symbol_hdr;
  pid_t mypid = 8;
  // check that we are loading the same symbol on both sides
  std::string expected_sym =
      "instance void [System.Private.CoreLib] "
      "System.Runtime.CompilerServices.AsyncTaskMethodBuilder`1+"
      "AsyncStateMachineBox`1[System.__Canon,System.Net.Http."
      "HttpConnectionPool+<CreateHttp11ConnectionAsync>d__100]::.ctor()["
      "OptimizedTier1]";

  // load jitdump on one side
  SymbolTable symbol_table;
  RuntimeSymbolLookup runtime_symbol_lookup("");
  ProcessAddress_t pc = 0x7fa12f0eac90;
  std::string jit_path =
      std::string(UNIT_TEST_DATA) + "/" + "jit-dotnet-8.dump";
  SymbolIdx_t symbol_idx = runtime_symbol_lookup.get_or_insert_jitdump(
      mypid, pc, symbol_table, symbol_hdr.profiles_dictionary(), jit_path);
  ASSERT_NE(symbol_idx, -1);
  const std::string sym_name =
      dict_string(symbol_hdr.profiles_dictionary(),
                  symbol_table[symbol_idx]._function_id->name);
  EXPECT_EQ(sym_name, expected_sym);
  {
    RuntimeSymbolLookup::Stats stats = runtime_symbol_lookup.get_stats();
    EXPECT_EQ(stats._symbol_count, 20809);
  }

  // load perfmap on the other
  RuntimeSymbolLookup runtime_symbol_lookup_perfmap(UNIT_TEST_DATA);
  SymbolTable symbol_table_perfmap;
  symbol_idx = runtime_symbol_lookup_perfmap.get_or_insert(
      mypid, pc, symbol_table_perfmap, symbol_hdr.profiles_dictionary());
  ASSERT_NE(symbol_idx, -1);
  const std::string sym_name_perfmap =
      dict_string(symbol_hdr.profiles_dictionary(),
                  symbol_table_perfmap[symbol_idx]._function_id->name);
  EXPECT_EQ(sym_name_perfmap, expected_sym);
  {
    RuntimeSymbolLookup::Stats stats =
        runtime_symbol_lookup_perfmap.get_stats();
    EXPECT_EQ(stats._symbol_count, 11605);
  }
}

namespace {
// Copy `src` into `dst`, truncating/replacing. Returns false on I/O error.
bool copy_file_replace(const std::string &src, const std::string &dst) {
  std::error_code ec;
  std::filesystem::copy_file(
      src, dst, std::filesystem::copy_options::overwrite_existing, ec);
  return !ec;
}

// Test fixture for jitdump growth-retry behaviour.
class JitdumpRetry : public ::testing::Test {
protected:
  void SetUp() override {
    // Unique per-test tempfile we can grow / replace.
    char tmpl[] = "/tmp/ddprof-jitdump-retry-XXXXXX";
    int fd = ::mkstemp(tmpl);
    ASSERT_GE(fd, 0);
    ::close(fd);
    _tmp_path = tmpl;
    _partial = std::string(UNIT_TEST_DATA) + "/jit-julia-partial.dump";
    _full = std::string(UNIT_TEST_DATA) + "/jit-simple-julia.dump";
    // The PC matching `julia_b_11` in `_full`.
    _matching_pc = 0x7bea23b00390;
  }
  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove(_tmp_path, ec);
  }

  std::string _tmp_path;
  std::string _partial;
  std::string _full;
  ProcessAddress_t _matching_pc;
};
} // namespace

TEST_F(JitdumpRetry, no_growth_does_not_reread) {
  SymbolHdr symbol_hdr;
  SymbolTable symbol_table;
  ASSERT_TRUE(copy_file_replace(_partial, _tmp_path));

  RuntimeSymbolLookup lookup("");
  pid_t mypid = getpid();

  // First call: reads partial file, PC not present, freezes the path.
  EXPECT_EQ(lookup.get_or_insert_jitdump(mypid, _matching_pc, symbol_table,
                                         symbol_hdr.profiles_dictionary(),
                                         _tmp_path),
            -1);
  EXPECT_EQ(lookup.get_stats()._nb_jit_reads, 1U);

  // Without growth, every retry tick observes the same (size, inode) and
  // does NOT trigger another read.
  const uint32_t budget = RuntimeSymbolLookup::jitdump_retry_check_every();
  for (uint32_t i = 0; i < 4U * budget; ++i) {
    lookup.get_or_insert_jitdump(mypid, _matching_pc, symbol_table,
                                 symbol_hdr.profiles_dictionary(), _tmp_path);
  }
  EXPECT_EQ(lookup.get_stats()._nb_jit_reads, 1U);
}

TEST_F(JitdumpRetry, growth_triggers_reread_and_resolves_pc) {
  SymbolHdr symbol_hdr;
  SymbolTable symbol_table;
  ASSERT_TRUE(copy_file_replace(_partial, _tmp_path));

  RuntimeSymbolLookup lookup("");
  pid_t mypid = getpid();

  // First read: partial file, PC misses → frozen.
  ASSERT_EQ(lookup.get_or_insert_jitdump(mypid, _matching_pc, symbol_table,
                                         symbol_hdr.profiles_dictionary(),
                                         _tmp_path),
            -1);
  ASSERT_EQ(lookup.get_stats()._nb_jit_reads, 1U);

  // Burn the retry budget while the file is unchanged: no extra reads.
  const uint32_t budget = RuntimeSymbolLookup::jitdump_retry_check_every();
  for (uint32_t i = 1; i < budget; ++i) {
    lookup.get_or_insert_jitdump(mypid, _matching_pc, symbol_table,
                                 symbol_hdr.profiles_dictionary(), _tmp_path);
  }
  EXPECT_EQ(lookup.get_stats()._nb_jit_reads, 1U);

  // Replace the file with a fuller jitdump that contains the PC.
  ASSERT_TRUE(copy_file_replace(_full, _tmp_path));

  // The next missed lookup should hit the stat tick, observe the change,
  // reparse the file, and resolve the PC.
  SymbolIdx_t idx =
      lookup.get_or_insert_jitdump(mypid, _matching_pc, symbol_table,
                                   symbol_hdr.profiles_dictionary(), _tmp_path);
  EXPECT_NE(idx, -1);
  EXPECT_EQ(lookup.get_stats()._nb_jit_reads, 2U);
}

TEST_F(JitdumpRetry, unreachable_path_does_not_churn) {
  // "//anon" reproduces the path-resolution pathology observed in CI:
  // the jitdump DSO ended up tagged with a placeholder filename. stat()
  // fails on every retry tick; we must not loop on reads.
  SymbolHdr symbol_hdr;
  SymbolTable symbol_table;
  RuntimeSymbolLookup lookup("");
  pid_t mypid = getpid();
  const std::string bogus = "//anon";

  EXPECT_EQ(lookup.get_or_insert_jitdump(mypid, 0xbadbeef, symbol_table,
                                         symbol_hdr.profiles_dictionary(),
                                         bogus),
            -1);
  const uint32_t initial_reads = lookup.get_stats()._nb_jit_reads;

  const uint32_t budget = RuntimeSymbolLookup::jitdump_retry_check_every();
  for (uint32_t i = 0; i < 4U * budget; ++i) {
    lookup.get_or_insert_jitdump(mypid, 0xbadbeef, symbol_table,
                                 symbol_hdr.profiles_dictionary(), bogus);
  }
  EXPECT_EQ(lookup.get_stats()._nb_jit_reads, initial_reads);
}

} // namespace ddprof
