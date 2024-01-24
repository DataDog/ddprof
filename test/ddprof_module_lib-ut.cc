// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_module_lib.hpp"

#include <gtest/gtest.h>

namespace ddprof {
Segment segment(Offset_t offset, uint32_t prot = PROT_EXEC | PROT_READ) {
  return {.addr = 0, .offset = offset, .prot = prot};
}

Mapping mapping(Offset_t offset, uint32_t prot = PROT_EXEC | PROT_READ) {
  return {.addr = 0, .offset = offset, .prot = prot};
}

TEST(ddprof_module_lib, empty) {
  auto res = find_match({}, {}, true);
  ASSERT_EQ(res.load_segment, nullptr);
  ASSERT_EQ(res.mapping, nullptr);
  ASSERT_FALSE(res.is_ambiguous);
}

TEST(ddprof_module_lib, empty_segments) {
  Mapping mappings[] = {mapping(0)};
  auto res = find_match(mappings, {}, true);
  ASSERT_EQ(res.load_segment, nullptr);
  ASSERT_EQ(res.mapping, nullptr);
  ASSERT_FALSE(res.is_ambiguous);
}

TEST(ddprof_module_lib, empty_mappings) {
  Segment segments[] = {segment(0x128)};
  auto res = find_match({}, segments, true);
  ASSERT_EQ(res.load_segment, nullptr);
  ASSERT_EQ(res.mapping, nullptr);
  ASSERT_FALSE(res.is_ambiguous);
}

TEST(ddprof_module_lib, simple) {
  Mapping mappings[] = {mapping(0)};
  Segment segments[] = {segment(0x128)};
  auto res = find_match(mappings, segments, true);
  ASSERT_EQ(res.load_segment, &segments[0]);
  ASSERT_EQ(res.mapping, &mappings[0]);
  ASSERT_FALSE(res.is_ambiguous);
}

TEST(ddprof_module_lib, simple2) {
  Mapping mappings[] = {mapping(0)};
  Segment segments[] = {segment(0, PROT_EXEC)};
  auto res = find_match(mappings, segments, true);
  ASSERT_EQ(res.load_segment, nullptr);
  ASSERT_EQ(res.mapping, nullptr);
  ASSERT_FALSE(res.is_ambiguous);
}

TEST(ddprof_module_lib, ambiguous) {
  Mapping mappings[] = {mapping(0)};
  Segment segments[] = {segment(0x128), segment(0x201)};
  auto res = find_match(mappings, segments, true);
  ASSERT_EQ(res.load_segment, &segments[0]);
  ASSERT_EQ(res.mapping, &mappings[0]);
  ASSERT_TRUE(res.is_ambiguous);
}

TEST(ddprof_module_lib, complex) {
  Mapping mappings[] = {mapping(0x1000), mapping(0x5000)};
  Segment segments[] = {segment(0x1100, PROT_EXEC), segment(0x1200),
                        segment(0x5128), segment(0x5457)};
  auto res = find_match(mappings, segments, true);
  ASSERT_EQ(res.load_segment, &segments[1]);
  ASSERT_EQ(res.mapping, &mappings[0]);
  ASSERT_TRUE(res.is_ambiguous);
}

TEST(ddprof_module_lib, libcoreclr) {
  Mapping mappings[] = {mapping(0x0), mapping(0x383000), mapping(0x384000),
                        mapping(0x5d9000)};
  Segment segments[] = {segment(0x000000, PROT_EXEC),
                        segment(0x6b7ec0, PROT_READ)};
  auto res = find_match(mappings, segments, true);
  ASSERT_FALSE(res.is_ambiguous);
}

} // namespace ddprof
