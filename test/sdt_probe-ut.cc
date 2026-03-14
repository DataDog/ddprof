// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "sdt_probe.hpp"
#include "loghandle.hpp"

#include <gtest/gtest.h>

namespace ddprof {

TEST(SDTProbeTest, ParseArgumentRegister) {
  // Test parsing register arguments
  auto arg = parse_sdt_argument("8@%rdi");
  ASSERT_TRUE(arg.has_value());
  EXPECT_EQ(arg->size, 8);
  EXPECT_EQ(arg->location, SDTArgLocation::kRegister);
  EXPECT_EQ(arg->raw_spec, "8@%rdi");

  // Negative size means signed
  arg = parse_sdt_argument("-4@%esi");
  ASSERT_TRUE(arg.has_value());
  EXPECT_EQ(arg->size, -4);
  EXPECT_EQ(arg->location, SDTArgLocation::kRegister);
}

TEST(SDTProbeTest, ParseArgumentMemory) {
  // Test parsing memory reference arguments
  auto arg = parse_sdt_argument("8@8(%rbp)");
  ASSERT_TRUE(arg.has_value());
  EXPECT_EQ(arg->size, 8);
  EXPECT_EQ(arg->location, SDTArgLocation::kMemory);
  EXPECT_EQ(arg->offset, 8);

  // Negative offset
  arg = parse_sdt_argument("4@-16(%rsp)");
  ASSERT_TRUE(arg.has_value());
  EXPECT_EQ(arg->size, 4);
  EXPECT_EQ(arg->location, SDTArgLocation::kMemory);
  EXPECT_EQ(arg->offset, -16);
}

TEST(SDTProbeTest, ParseArgumentConstant) {
  // Test parsing constant arguments
  auto arg = parse_sdt_argument("4@$42");
  ASSERT_TRUE(arg.has_value());
  EXPECT_EQ(arg->size, 4);
  EXPECT_EQ(arg->location, SDTArgLocation::kConstant);
  EXPECT_EQ(arg->offset, 42);

  // Negative constant
  arg = parse_sdt_argument("8@$-1");
  ASSERT_TRUE(arg.has_value());
  EXPECT_EQ(arg->size, 8);
  EXPECT_EQ(arg->location, SDTArgLocation::kConstant);
  EXPECT_EQ(arg->offset, -1);
}

TEST(SDTProbeTest, ParseArgumentInvalid) {
  // Test invalid argument specs
  EXPECT_FALSE(parse_sdt_argument("").has_value());
  EXPECT_FALSE(parse_sdt_argument("invalid").has_value());
  EXPECT_FALSE(parse_sdt_argument("@%rdi").has_value());
  EXPECT_FALSE(parse_sdt_argument("8@").has_value());
}

TEST(SDTProbeTest, X86RegNameToPerf) {
  // Test x86-64 register name to perf register mapping
#ifdef __x86_64__
  EXPECT_GE(x86_reg_name_to_perf_reg("rax"), 0);
  EXPECT_GE(x86_reg_name_to_perf_reg("rdi"), 0);
  EXPECT_GE(x86_reg_name_to_perf_reg("rsi"), 0);
  EXPECT_GE(x86_reg_name_to_perf_reg("rdx"), 0);
  EXPECT_GE(x86_reg_name_to_perf_reg("rcx"), 0);
  EXPECT_GE(x86_reg_name_to_perf_reg("r8"), 0);
  EXPECT_GE(x86_reg_name_to_perf_reg("r9"), 0);
  EXPECT_GE(x86_reg_name_to_perf_reg("rsp"), 0);
  EXPECT_GE(x86_reg_name_to_perf_reg("rbp"), 0);

  // 32-bit register names should also work
  EXPECT_GE(x86_reg_name_to_perf_reg("eax"), 0);
  EXPECT_GE(x86_reg_name_to_perf_reg("edi"), 0);
  EXPECT_GE(x86_reg_name_to_perf_reg("esi"), 0);

  // Invalid register names
  EXPECT_EQ(x86_reg_name_to_perf_reg("invalid"), -1);
  EXPECT_EQ(x86_reg_name_to_perf_reg(""), -1);
#endif
}

TEST(SDTProbeTest, SDTProbeSetFindProbe) {
  SDTProbeSet probe_set;
  probe_set.binary_path = "/test/binary";

  SDTProbe probe1;
  probe1.provider = "ddprof_malloc";
  probe1.name = "entry";
  probe1.address = 0x1000;
  probe_set.probes.push_back(probe1);

  SDTProbe probe2;
  probe2.provider = "ddprof_malloc";
  probe2.name = "exit";
  probe2.address = 0x2000;
  probe_set.probes.push_back(probe2);

  SDTProbe probe3;
  probe3.provider = "ddprof_free";
  probe3.name = "entry";
  probe3.address = 0x3000;
  probe_set.probes.push_back(probe3);

  // Find single probe
  const SDTProbe *found = probe_set.find_probe("ddprof_malloc", "entry");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->address, 0x1000);

  found = probe_set.find_probe("ddprof_malloc", "exit");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->address, 0x2000);

  found = probe_set.find_probe("ddprof_free", "entry");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->address, 0x3000);

  // Probe not found
  found = probe_set.find_probe("ddprof_free", "exit");
  EXPECT_EQ(found, nullptr);

  found = probe_set.find_probe("unknown", "entry");
  EXPECT_EQ(found, nullptr);
}

TEST(SDTProbeTest, SDTProbeSetFindProbes) {
  SDTProbeSet probe_set;
  probe_set.binary_path = "/test/binary";

  // Add multiple probes with same provider/name (could happen with multiple
  // call sites)
  SDTProbe probe1;
  probe1.provider = "test";
  probe1.name = "point";
  probe1.address = 0x1000;
  probe_set.probes.push_back(probe1);

  SDTProbe probe2;
  probe2.provider = "test";
  probe2.name = "point";
  probe2.address = 0x2000;
  probe_set.probes.push_back(probe2);

  SDTProbe probe3;
  probe3.provider = "other";
  probe3.name = "point";
  probe3.address = 0x3000;
  probe_set.probes.push_back(probe3);

  auto probes = probe_set.find_probes("test", "point");
  EXPECT_EQ(probes.size(), 2);

  probes = probe_set.find_probes("other", "point");
  EXPECT_EQ(probes.size(), 1);

  probes = probe_set.find_probes("unknown", "unknown");
  EXPECT_EQ(probes.size(), 0);
}

TEST(SDTProbeTest, SDTProbeSetHasAllocationProbes) {
  SDTProbeSet probe_set;
  probe_set.binary_path = "/test/binary";

  // Empty set should not have allocation probes
  EXPECT_FALSE(probe_set.has_allocation_probes());

  // Add malloc entry
  SDTProbe probe1;
  probe1.provider = "ddprof_malloc";
  probe1.name = "entry";
  probe_set.probes.push_back(probe1);
  EXPECT_FALSE(probe_set.has_allocation_probes());

  // Add malloc exit
  SDTProbe probe2;
  probe2.provider = "ddprof_malloc";
  probe2.name = "exit";
  probe_set.probes.push_back(probe2);
  EXPECT_FALSE(probe_set.has_allocation_probes());

  // Add free entry - now we have all required probes
  SDTProbe probe3;
  probe3.provider = "ddprof_free";
  probe3.name = "entry";
  probe_set.probes.push_back(probe3);
  EXPECT_TRUE(probe_set.has_allocation_probes());
}

TEST(SDTProbeTest, GetProbeType) {
  SDTProbe probe;

  probe.provider = "ddprof_malloc";
  probe.name = "entry";
  EXPECT_EQ(SDTProbeSet::get_probe_type(probe), SDTProbeType::kMallocEntry);

  probe.name = "exit";
  EXPECT_EQ(SDTProbeSet::get_probe_type(probe), SDTProbeType::kMallocExit);

  probe.provider = "ddprof_free";
  probe.name = "entry";
  EXPECT_EQ(SDTProbeSet::get_probe_type(probe), SDTProbeType::kFreeEntry);

  probe.name = "exit";
  EXPECT_EQ(SDTProbeSet::get_probe_type(probe), SDTProbeType::kFreeExit);

  probe.provider = "unknown";
  probe.name = "unknown";
  EXPECT_EQ(SDTProbeSet::get_probe_type(probe), SDTProbeType::kUnknown);
}

TEST(SDTProbeTest, SDTProbeFullName) {
  SDTProbe probe;
  probe.provider = "ddprof_malloc";
  probe.name = "entry";
  EXPECT_EQ(probe.full_name(), "ddprof_malloc:entry");
}

// Test parsing SDT probes from a non-existent file
TEST(SDTProbeTest, ParseNonExistentFile) {
  auto result = parse_sdt_probes("/non/existent/file");
  EXPECT_FALSE(result.has_value());
}

// Test parsing SDT probes from a file without SDT probes
TEST(SDTProbeTest, ParseFileWithoutProbes) {
  // Use /bin/ls as a test binary that likely doesn't have our specific probes
  auto result = parse_sdt_probes("/bin/ls");
  // May or may not have SDT probes, but shouldn't crash
  // If it has probes, they won't be our allocation probes
  if (result.has_value()) {
    EXPECT_FALSE(result->has_allocation_probes());
  }
}

} // namespace ddprof
