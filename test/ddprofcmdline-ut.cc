// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_cmdline.h"
#include "perf_watcher.h"

#include <gtest/gtest.h>

static char const *const sTestPaterns[] = {"cAn", "yUo", "eVen", "tYpe"};

TEST(CmdLineTst, ArgWhich) {
  ASSERT_EQ(arg_which("tYpe", sTestPaterns, 4), 3);
  ASSERT_EQ(arg_which("type", sTestPaterns, 4), 3);
  ASSERT_EQ(arg_which("typo", sTestPaterns, 4), -1);
  ASSERT_FALSE(arg_inset("typo", sTestPaterns, 4));
  ASSERT_TRUE(arg_inset("tYpe", sTestPaterns, 4));
}

TEST(CmdLineTst, ArgYesNo) {
  const char *yesStr = "YeS";
  const char *noStr = "nO";
  ASSERT_TRUE(arg_yesno(yesStr, 1));
  ASSERT_FALSE(arg_yesno(noStr, 1));
  ASSERT_TRUE(arg_yesno(noStr, 0));
  ASSERT_FALSE(arg_yesno(yesStr, 0));
}

TEST(CmdLineTst, PartialFilled) {
  const char *partialPaterns[] = {"cAn", "temp", "eVen", "tYpe"};
  ASSERT_EQ(arg_which("temp", partialPaterns, 4), 1);
  partialPaterns[1] = nullptr; // one of the strings is null
  ASSERT_EQ(arg_which("typo", partialPaterns, 4),
            -1); // Check that we can iterate safely over everything
  ASSERT_TRUE(arg_inset("tYpe", partialPaterns, 4));
}

TEST(CmdLineTst, NullPatterns) {
  char const *const *testPaterns =
      nullptr; // the actual pointers are not const, only the value inside of
               // the pointers
  ASSERT_EQ(arg_which("typo", testPaterns, 4),
            -1); // Check that we can iterate safely over everything
}

TEST(CmdLineTst, FirstEventHit) {
  char const *str = "hCPU";
  PerfWatcher watcher = {};
  ASSERT_TRUE(watcher_from_event(str, &watcher));
  ASSERT_EQ(watcher.type, PERF_TYPE_HARDWARE);
  ASSERT_EQ(watcher.type, PERF_COUNT_HW_CPU_CYCLES);
}

TEST(CmdLineTst, LastEventHit) {
  int idx = DDPROF_PWE_LENGTH - 1;
  const PerfWatcher *w1 = ewatcher_from_idx(idx);
  const PerfWatcher *w2 =
      ewatcher_from_str("sALLOC"); // should be the last watcher
  ASSERT_NE(nullptr, w1);
  ASSERT_NE(nullptr, w2);
  ASSERT_EQ(w1, w2);
}

TEST(CmdLineTst, LiteralEventWithGoodValue) {
  char const *str = "hCPU,555";
  PerfWatcher watcher = {};
  watcher.sample_period = 12345;
  ASSERT_TRUE(watcher_from_event(str, &watcher));
  ASSERT_EQ(watcher.sample_period, 555); // value changed
}

// An event without a separator is invalid, even if the components are valid.
// This is because we may wish to have event types which end in a number at some
// point.
TEST(CmdLineTst, LiteralEventWithNoComma) {
  char const *str = "hCPU1";
  PerfWatcher watcher = {};
  ASSERT_FALSE(watcher_from_event(str, &watcher));
}

TEST(CmdLineTst, LiteralEventWithVeryBadValue) {
  char const *str = "hCPU,apples";
  PerfWatcher watcher = {};
  ASSERT_FALSE(watcher_from_event(str, &watcher));
}

TEST(CmdLineTst, LiteralEventWithKindaBadValue) {
  char const *str = "hCPU,123apples";
  PerfWatcher watcher = {};
  ASSERT_FALSE(watcher_from_event(str, &watcher));
}
