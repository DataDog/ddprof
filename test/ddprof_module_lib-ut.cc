// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_module_lib.hpp"

#include <gtest/gtest.h>

namespace ddprof {

TEST(ddprof_module_lib, find_build_id) {
  // init elf library
  elf_version(EV_CURRENT);

  {
    auto build_id = find_build_id(UNIT_TEST_DATA "/gnu_exe");
    ASSERT_TRUE(build_id.has_value());
    ASSERT_EQ(*build_id, "463bf6f201611ff6bda58b492c39760bdf91c64c");
  }

  {
    auto build_id = find_build_id(UNIT_TEST_DATA "/gnu_exe_without_sections");
    ASSERT_TRUE(build_id.has_value());
    ASSERT_EQ(*build_id, "463bf6f201611ff6bda58b492c39760bdf91c64c");
  }

  {
    auto build_id = find_build_id(UNIT_TEST_DATA "/go_exe.debug");
    ASSERT_TRUE(build_id.has_value());
    ASSERT_EQ(*build_id,
              "1QJNd3IcsGXYu2DBSgMt/-RUtp0ZCapQufd_qb_Yc/iaqdEc--v2HiCZnsxjI6/"
              "ljHvxz7xDEEo-TQ3z9Op");
  }
}
} // namespace ddprof
