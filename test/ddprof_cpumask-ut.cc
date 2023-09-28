// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "ddprof_cpumask.hpp"

#include <sched.h>

namespace ddprof {

TEST(ddprof_cpumask, nprocessors_conf) {
  int ncpus = nprocessors_conf();
  cpu_set_t cpus;
  CPU_ZERO(&cpus);
  CPU_SET(0, &cpus);
  sched_setaffinity(0, sizeof(cpu_set_t), &cpus);
  // nprocessors_conf should not depend on affinity
  ASSERT_EQ(ncpus, nprocessors_conf());
}

TEST(ddprof_cpumask, parse_cpu_mask) {
  cpu_set_t cpus;
  ASSERT_TRUE(parse_cpu_mask("0x1", cpus));
  ASSERT_EQ(CPU_COUNT(&cpus), 1);
  ASSERT_TRUE(CPU_ISSET(0, &cpus));
  ASSERT_TRUE(parse_cpu_mask("1", cpus));
  ASSERT_EQ(CPU_COUNT(&cpus), 1);
  ASSERT_TRUE(CPU_ISSET(0, &cpus));
  ASSERT_TRUE(parse_cpu_mask("10", cpus));
  ASSERT_EQ(CPU_COUNT(&cpus), 1);
  ASSERT_TRUE(CPU_ISSET(4, &cpus));
  ASSERT_TRUE(parse_cpu_mask("f", cpus));
  ASSERT_EQ(CPU_COUNT(&cpus), 4);
  ASSERT_TRUE(CPU_ISSET(0, &cpus));
  ASSERT_TRUE(CPU_ISSET(1, &cpus));
  ASSERT_TRUE(CPU_ISSET(2, &cpus));
  ASSERT_TRUE(CPU_ISSET(3, &cpus));
  ASSERT_TRUE(parse_cpu_mask("F", cpus));
  ASSERT_EQ(CPU_COUNT(&cpus), 4);
  ASSERT_TRUE(CPU_ISSET(0, &cpus));
  ASSERT_TRUE(CPU_ISSET(1, &cpus));
  ASSERT_TRUE(CPU_ISSET(2, &cpus));
  ASSERT_TRUE(CPU_ISSET(3, &cpus));
  ASSERT_TRUE(parse_cpu_mask("100000000", cpus));
  ASSERT_EQ(CPU_COUNT(&cpus), 1);
  ASSERT_TRUE(CPU_ISSET(32, &cpus));
  ASSERT_TRUE(parse_cpu_mask("1,00000000", cpus));
  ASSERT_EQ(CPU_COUNT(&cpus), 1);
  ASSERT_TRUE(CPU_ISSET(32, &cpus));
}

} // namespace ddprof
