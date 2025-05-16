// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2025-Present
// Datadog, Inc.

#include "perf_watcher.hpp"
#include "presets.hpp"

#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

using namespace ddprof;

TEST(presets, default_preset) {
  std::vector<PerfWatcher> watchers;
  DDRes res =
      add_preset("default", false, k_default_perf_stack_sample_size, watchers);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_EQ(watchers.size(), 2);

  // Check CPU watcher
  auto cpu_it =
      std::find_if(watchers.begin(), watchers.end(), [](const auto &w) {
        return w.ddprof_event_type == DDPROF_PWE_sCPU;
      });
  EXPECT_NE(cpu_it, watchers.end());
  EXPECT_EQ(cpu_it->aggregation_mode, EventAggregationMode::kSum);

  // Check ALLOC watcher
  auto alloc_it =
      std::find_if(watchers.begin(), watchers.end(), [](const auto &w) {
        return w.ddprof_event_type == DDPROF_PWE_sALLOC;
      });
  EXPECT_NE(alloc_it, watchers.end());
  EXPECT_EQ(alloc_it->aggregation_mode, EventAggregationMode::kSum);
}

TEST(presets, default_pid_preset) {
  std::vector<PerfWatcher> watchers;
  DDRes res =
      add_preset("default", true, k_default_perf_stack_sample_size, watchers);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_EQ(watchers.size(), 1);

  // Check CPU watcher
  auto cpu_it =
      std::find_if(watchers.begin(), watchers.end(), [](const auto &w) {
        return w.ddprof_event_type == DDPROF_PWE_sCPU;
      });
  EXPECT_NE(cpu_it, watchers.end());
  EXPECT_EQ(cpu_it->aggregation_mode, EventAggregationMode::kSum);
}

TEST(presets, cpu_only_preset) {
  std::vector<PerfWatcher> watchers;
  DDRes res =
      add_preset("cpu_only", false, k_default_perf_stack_sample_size, watchers);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_EQ(watchers.size(), 1);

  // Check CPU watcher
  auto cpu_it =
      std::find_if(watchers.begin(), watchers.end(), [](const auto &w) {
        return w.ddprof_event_type == DDPROF_PWE_sCPU;
      });
  EXPECT_NE(cpu_it, watchers.end());
  EXPECT_EQ(cpu_it->aggregation_mode, EventAggregationMode::kSum);
}

TEST(presets, alloc_only_preset) {
  std::vector<PerfWatcher> watchers;
  DDRes res = add_preset("alloc_only", false, k_default_perf_stack_sample_size,
                         watchers);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_EQ(watchers.size(), 1);

  // Check ALLOC watcher
  auto alloc_it =
      std::find_if(watchers.begin(), watchers.end(), [](const auto &w) {
        return w.ddprof_event_type == DDPROF_PWE_sALLOC;
      });
  EXPECT_NE(alloc_it, watchers.end());
  EXPECT_EQ(alloc_it->aggregation_mode, EventAggregationMode::kSum);
}

TEST(presets, cpu_live_heap_preset) {
  std::vector<PerfWatcher> watchers;
  DDRes res = add_preset("cpu_live_heap", false,
                         k_default_perf_stack_sample_size, watchers);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_EQ(watchers.size(), 2);

  // Check CPU watcher
  auto cpu_it =
      std::find_if(watchers.begin(), watchers.end(), [](const auto &w) {
        return w.ddprof_event_type == DDPROF_PWE_sCPU;
      });
  EXPECT_NE(cpu_it, watchers.end());
  EXPECT_EQ(cpu_it->aggregation_mode, EventAggregationMode::kSum);

  // Check ALLOC watcher with live mode
  auto alloc_it =
      std::find_if(watchers.begin(), watchers.end(), [](const auto &w) {
        return w.ddprof_event_type == DDPROF_PWE_sALLOC;
      });
  EXPECT_NE(alloc_it, watchers.end());
  EXPECT_EQ(alloc_it->aggregation_mode,
            EventAggregationMode::kLiveSum | EventAggregationMode::kSum);
}

TEST(presets, alloc_live_heap_preset) {
  std::vector<PerfWatcher> watchers;
  DDRes res = add_preset("alloc_live_heap", false,
                         k_default_perf_stack_sample_size, watchers);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_EQ(watchers.size(), 1);

  // Check ALLOC watcher with live mode
  auto alloc_it =
      std::find_if(watchers.begin(), watchers.end(), [](const auto &w) {
        return w.ddprof_event_type == DDPROF_PWE_sALLOC;
      });
  EXPECT_NE(alloc_it, watchers.end());
  EXPECT_EQ(alloc_it->aggregation_mode,
            EventAggregationMode::kLiveSum | EventAggregationMode::kSum);
}

TEST(presets, invalid_preset) {
  std::vector<PerfWatcher> watchers;
  DDRes res = add_preset("invalid_preset", false,
                         k_default_perf_stack_sample_size, watchers);
  EXPECT_FALSE(IsDDResOK(res));
  EXPECT_EQ(watchers.size(), 0);
}
