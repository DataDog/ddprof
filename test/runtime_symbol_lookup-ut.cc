// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "loghandle.hpp"
#include "runtime_symbol_lookup.hpp"
#include "symbol_table.hpp"

#include <string>

namespace ddprof {

TEST(runtime_symbol_lookup, no_map) {
  LogHandle log_handle;
  SymbolTable symbol_table;
  RuntimeSymbolLookup runtime_symbol_lookup(UNIT_TEST_DATA);
  ProcessAddress_t pc = 0x7FB0614BB980;
  // no pid 43
  SymbolIdx_t symbol_idx =
      runtime_symbol_lookup.get_or_insert(43, pc, symbol_table);
  // We expect no symbols to be found for this pid
  ASSERT_EQ(symbol_idx, -1);
}

TEST(runtime_symbol_lookup, parse_map) {
  LogHandle log_handle;
  SymbolTable symbol_table;
  RuntimeSymbolLookup runtime_symbol_lookup(UNIT_TEST_DATA);
  // reads a file with symbols generated from .NET
  ProcessAddress_t pc = 0x7FB0614BB980;
  SymbolIdx_t symbol_idx =
      runtime_symbol_lookup.get_or_insert(42, pc, symbol_table);
  ASSERT_NE(symbol_idx, -1);
  ASSERT_TRUE(symbol_table[symbol_idx]._symname.find(
                  "RuntimeEnvironmentInfo::get_OsPlatform") !=
              std::string::npos);
}

TEST(runtime_symbol_lookup, overflow) {
  LogHandle log_handle;
  SymbolTable symbol_table;
  RuntimeSymbolLookup runtime_symbol_lookup(UNIT_TEST_DATA);
  // reads a file with symbols generated from .NET
  {
    ProcessAddress_t pc = 0x00007FB06149E6A0;
    SymbolIdx_t symbol_idx =
        runtime_symbol_lookup.get_or_insert(1, pc, symbol_table);
    ASSERT_NE(symbol_idx, -1);
    LG_NFO("%s", symbol_table[symbol_idx]._symname.c_str());
    ASSERT_TRUE(symbol_table[symbol_idx]._symname.size() <= 300);
  }
  {
    ProcessAddress_t pc = 0xFFFFFFFFFFFFFFFE;
    SymbolIdx_t symbol_idx =
        runtime_symbol_lookup.get_or_insert(1, pc, symbol_table);
    ASSERT_EQ(symbol_idx, -1);
  }
}

TEST(runtime_symbol_lookup, jitdump_simple) {
  LogHandle log_handle;
  pid_t mypid = getpid();
  SymbolTable symbol_table;
  RuntimeSymbolLookup runtime_symbol_lookup(UNIT_TEST_DATA);
  ProcessAddress_t pc = 0x7bea23b00390;
  std::string jit_path =
      std::string(UNIT_TEST_DATA) + "/" + std::string("jit.dump");
  SymbolIdx_t symbol_idx = runtime_symbol_lookup.get_or_insert_jitdump(
      mypid, pc, symbol_table, jit_path);
  ASSERT_NE(symbol_idx, -1);
  ASSERT_EQ(std::string("julia_b_11"), symbol_table[symbol_idx]._demangle_name);
}

TEST(runtime_symbol_lookup, jitdump_override) {
  // test what happens when the file is altered
}

TEST(runtime_symbol_lookup, lock_file) {
  // test that we are locking the file
}

} // namespace ddprof
