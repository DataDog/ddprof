// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "loghandle.hpp"
#include "runtime_symbol_lookup.hpp"
#include "symbol_hdr.hpp"
#include "symbol_table.hpp"

#include <datadog/profiling.h>
#include <string>

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

} // namespace ddprof
