// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "mapinfo_lookup.hpp"

#include "dso.hpp"
#include "mapinfo_table.hpp"

#include "datadog/profiling.h"

#include <gtest/gtest.h>
#include <sys/mman.h>

namespace ddprof {
namespace {

Dso make_dso(pid_t pid, ProcessAddress_t start, ProcessAddress_t end,
             Offset_t offset, std::string filename) {
  return Dso{pid, start, end, offset, std::move(filename), 0, PROT_EXEC};
}

class TestDictionary {
public:
  TestDictionary() {
    ddog_prof_Status status = ddog_prof_ProfilesDictionary_new(&_dict);
    EXPECT_EQ(status.err, nullptr);
    if (status.err != nullptr) {
      ddog_prof_Status_drop(&status);
    }
  }

  ~TestDictionary() {
    if (_dict) {
      ddog_prof_ProfilesDictionary_drop(&_dict);
    }
  }

  [[nodiscard]] const ddog_prof_ProfilesDictionary *get() const {
    return _dict;
  }

private:
  ddog_prof_ProfilesDictionaryHandle _dict{};
};

} // namespace

TEST(MapInfoLookupTest, cache_hit_returns_same_row) {
  MapInfoLookup lookup;
  MapInfoTable table;
  TestDictionary dict;
  constexpr pid_t pid = 42;

  Dso const dso = make_dso(pid, 0x1000, 0x2000, 0, "/lib/libfoo.so");
  auto const idx1 =
      lookup.get_or_insert(pid, table, dso, std::nullopt, dict.get());
  auto const idx2 =
      lookup.get_or_insert(pid, table, dso, std::nullopt, dict.get());
  EXPECT_EQ(idx1, idx2);
  EXPECT_EQ(table.size(), 1U);
}

// A process remaps a different object at the same start address. The cache
// must detect the identity mismatch (high_addr / offset) and allocate
// a fresh row rather than reusing the stale one.
TEST(MapInfoLookupTest, remap_same_start_different_object) {
  MapInfoLookup lookup;
  MapInfoTable table;
  TestDictionary dict;
  constexpr pid_t pid = 42;

  Dso const foo = make_dso(pid, 0x1000, 0x2000, 0, "/lib/libfoo.so");
  auto const idx_foo =
      lookup.get_or_insert(pid, table, foo, std::nullopt, dict.get());
  ASSERT_EQ(idx_foo, 0U);

  // Same start, but a different object: different end, different path.
  Dso const bar = make_dso(pid, 0x1000, 0x1800, 0, "/lib/libbar.so");
  auto const idx_bar =
      lookup.get_or_insert(pid, table, bar, std::nullopt, dict.get());

  EXPECT_NE(idx_bar, idx_foo)
      << "stale cache entry was reused for a different object";
  ASSERT_EQ(table.size(), 2U);
  // Old row retained so prior indexes stay valid; the rows are distinct
  // entries.
  EXPECT_NE(idx_foo, idx_bar);
}

TEST(MapInfoLookupTest, filename_mismatch_invalidates) {
  MapInfoLookup lookup;
  MapInfoTable table;
  TestDictionary dict;
  constexpr pid_t pid = 42;

  Dso const foo = make_dso(pid, 0x1000, 0x2000, 0, "/lib/libfoo.so");
  Dso const bar = make_dso(pid, 0x1000, 0x2000, 0, "/lib/libbar.so");

  auto const idx_foo =
      lookup.get_or_insert(pid, table, foo, std::nullopt, dict.get());
  auto const idx_bar =
      lookup.get_or_insert(pid, table, bar, std::nullopt, dict.get());

  EXPECT_NE(idx_foo, idx_bar);
  EXPECT_EQ(table.size(), 2U);
}

TEST(MapInfoLookupTest, build_id_mismatch_invalidates) {
  MapInfoLookup lookup;
  MapInfoTable table;
  TestDictionary dict;
  constexpr pid_t pid = 7;

  Dso const dso = make_dso(pid, 0x4000, 0x5000, 0x100, "/opt/app/libapp.so");
  auto const idx_v1 = lookup.get_or_insert(
      pid, table, dso, BuildIdStr{"aaaaaaaaaa"}, dict.get());
  auto const idx_v2 = lookup.get_or_insert(
      pid, table, dso, BuildIdStr{"bbbbbbbbbb"}, dict.get());

  EXPECT_NE(idx_v1, idx_v2);
  EXPECT_EQ(table.size(), 2U);
}

TEST(MapInfoLookupTest, different_pids_are_isolated) {
  MapInfoLookup lookup;
  MapInfoTable table;
  TestDictionary dict;

  Dso const dso_a = make_dso(100, 0x1000, 0x2000, 0, "/lib/libx.so");
  Dso const dso_b = make_dso(200, 0x1000, 0x2000, 0, "/lib/libx.so");
  auto const idx_a =
      lookup.get_or_insert(100, table, dso_a, std::nullopt, dict.get());
  auto const idx_b =
      lookup.get_or_insert(200, table, dso_b, std::nullopt, dict.get());

  // Same start address, different pid - each must get its own row.
  EXPECT_NE(idx_a, idx_b);
}

TEST(MapInfoLookupTest, erase_pid_does_not_shrink_table) {
  MapInfoLookup lookup;
  MapInfoTable table;
  TestDictionary dict;
  constexpr pid_t pid = 42;

  Dso const dso = make_dso(pid, 0x1000, 0x2000, 0, "/lib/libfoo.so");
  lookup.get_or_insert(pid, table, dso, std::nullopt, dict.get());
  ASSERT_EQ(table.size(), 1U);

  lookup.erase(pid);
  // Table rows persist so any previously-captured MapInfoIdx_t stays valid.
  EXPECT_EQ(table.size(), 1U);

  // After erase, the next get_or_insert for that pid allocates a fresh row.
  auto const idx =
      lookup.get_or_insert(pid, table, dso, std::nullopt, dict.get());
  EXPECT_EQ(idx, 1U);
  EXPECT_EQ(table.size(), 2U);
}

} // namespace ddprof
