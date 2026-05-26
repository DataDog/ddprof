// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "live_allocation_snapshot.hpp"

#include <gtest/gtest.h>

namespace ddprof::live_alloc_snapshot {

namespace {

Snapshot make_sample_snapshot() {
  Snapshot s;
  UnwindOutputPortable uo;
  uo.pid = 100;
  uo.tid = 101;
  uo.container_id = "ctr-abc";
  uo.exe_name = "/usr/bin/myapp";
  uo.thread_name = "worker-0";
  uo.locs.push_back({/*ip*/ 0x1000, /*elf_addr*/ 0x500, /*lineno*/ 42,
                     "malloc", "__libc_malloc", "malloc.c",
                     /*map_low*/ 0x10000, /*map_high*/ 0x20000,
                     /*map_offset*/ 0, "/lib/libc.so", "abcdef0123"});
  uo.locs.push_back({0x2000, 0x600, 0, "main", "", "main.c", 0x30000, 0x40000,
                     0, "/usr/bin/myapp", ""});
  s.stacks.push_back(std::move(uo));

  UnwindOutputPortable uo2;
  uo2.pid = 200;
  uo2.tid = 200;
  uo2.locs.push_back({0x3000, 0x700, 7, "fn", "", "f.c", 0, 0, 0, "", ""});
  s.stacks.push_back(std::move(uo2));

  PidEntry pe;
  pe.watcher_pos = 0;
  pe.pid = 100;
  pe.address_conflict_count = 3;
  pe.tracked_address_count = 17;
  pe.addresses.push_back({0xdead0000, 1024, 0});
  pe.addresses.push_back({0xdead1000, 2048, 0});
  pe.addresses.push_back({0xdead2000, 4096, 1});
  // One synthetic-cleared address.
  pe.addresses.push_back({0xdead3000, 32, k_cleared_stack_idx});
  s.pids.push_back(std::move(pe));

  s.cleared_addresses = 1;
  s.dropped_pids = 0;
  return s;
}

void expect_eq(const UnwindOutputPortable &a, const UnwindOutputPortable &b) {
  EXPECT_EQ(a.pid, b.pid);
  EXPECT_EQ(a.tid, b.tid);
  EXPECT_EQ(a.container_id, b.container_id);
  EXPECT_EQ(a.exe_name, b.exe_name);
  EXPECT_EQ(a.thread_name, b.thread_name);
  ASSERT_EQ(a.locs.size(), b.locs.size());
  for (size_t i = 0; i < a.locs.size(); ++i) {
    const auto &x = a.locs[i];
    const auto &y = b.locs[i];
    EXPECT_EQ(x.ip, y.ip);
    EXPECT_EQ(x.elf_addr, y.elf_addr);
    EXPECT_EQ(x.lineno, y.lineno);
    EXPECT_EQ(x.fn_name, y.fn_name);
    EXPECT_EQ(x.fn_system_name, y.fn_system_name);
    EXPECT_EQ(x.fn_file, y.fn_file);
    EXPECT_EQ(x.map_low, y.map_low);
    EXPECT_EQ(x.map_high, y.map_high);
    EXPECT_EQ(x.map_offset, y.map_offset);
    EXPECT_EQ(x.map_filename, y.map_filename);
    EXPECT_EQ(x.map_build_id, y.map_build_id);
  }
}

} // namespace

TEST(LiveAllocationSnapshotTest, RoundTripBinary) {
  Snapshot in = make_sample_snapshot();
  std::vector<uint8_t> buf;
  serialize(in, buf);
  ASSERT_FALSE(buf.empty());

  Snapshot out;
  ASSERT_TRUE(deserialize(buf.data(), buf.size(), out));

  ASSERT_EQ(in.stacks.size(), out.stacks.size());
  for (size_t i = 0; i < in.stacks.size(); ++i) {
    expect_eq(in.stacks[i], out.stacks[i]);
  }
  ASSERT_EQ(in.pids.size(), out.pids.size());
  for (size_t i = 0; i < in.pids.size(); ++i) {
    const auto &a = in.pids[i];
    const auto &b = out.pids[i];
    EXPECT_EQ(a.watcher_pos, b.watcher_pos);
    EXPECT_EQ(a.pid, b.pid);
    EXPECT_EQ(a.address_conflict_count, b.address_conflict_count);
    EXPECT_EQ(a.tracked_address_count, b.tracked_address_count);
    ASSERT_EQ(a.addresses.size(), b.addresses.size());
    for (size_t j = 0; j < a.addresses.size(); ++j) {
      EXPECT_EQ(a.addresses[j].addr, b.addresses[j].addr);
      EXPECT_EQ(a.addresses[j].value, b.addresses[j].value);
      EXPECT_EQ(a.addresses[j].stack_idx, b.addresses[j].stack_idx);
    }
  }
  EXPECT_EQ(in.cleared_addresses, out.cleared_addresses);
  EXPECT_EQ(in.dropped_pids, out.dropped_pids);
}

TEST(LiveAllocationSnapshotTest, RejectsBadMagic) {
  Snapshot in = make_sample_snapshot();
  std::vector<uint8_t> buf;
  serialize(in, buf);
  buf[0] = 'X';
  Snapshot out;
  EXPECT_FALSE(deserialize(buf.data(), buf.size(), out));
}

TEST(LiveAllocationSnapshotTest, RejectsTruncated) {
  Snapshot in = make_sample_snapshot();
  std::vector<uint8_t> buf;
  serialize(in, buf);
  Snapshot out;
  EXPECT_FALSE(deserialize(buf.data(), buf.size() - 4, out));
}

TEST(LiveAllocationSnapshotTest, EmptySnapshotRoundTrip) {
  Snapshot in;
  std::vector<uint8_t> buf;
  serialize(in, buf);
  Snapshot out;
  ASSERT_TRUE(deserialize(buf.data(), buf.size(), out));
  EXPECT_TRUE(out.stacks.empty());
  EXPECT_TRUE(out.pids.empty());
  EXPECT_EQ(out.cleared_addresses, 0u);
  EXPECT_EQ(out.dropped_pids, 0u);
}

} // namespace ddprof::live_alloc_snapshot
