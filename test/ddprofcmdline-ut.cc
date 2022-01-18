// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

extern "C" {
#include "ddprof_cmdline.h"
#include "perf_option.h"
}

#include <gtest/gtest.h>

static char const *const sTestPaterns[] = {"cAn", "yUo", "eVen", "tYpe"};

TEST(CmdLineTst, PerfoptionMatchSize) { ASSERT_TRUE(perfoptions_match_size()); }

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
  char const *str = perfoptions_lookup_idx(0);
  size_t i = 999999;
  uint64_t val = 0;
  ASSERT_TRUE(process_event(str, perfoptions_lookup(), perfoptions_nb_presets(),
                            &i, &val));
  ASSERT_EQ(i, 0);
}

TEST(CmdLineTst, LastEventHit) {
  char const *str = perfoptions_lookup_idx(perfoptions_nb_presets() - 1);
  size_t i = 999999;
  uint64_t val = 0;
  ASSERT_TRUE(process_event(str, perfoptions_lookup(), perfoptions_nb_presets(),
                            &i, &val));
  ASSERT_EQ(i, perfoptions_nb_presets() - 1);
}

TEST(CmdLineTst, LiteralEventWithGoodValue) {
  char const *str = "hCPU,555";
  size_t i = 999999;
  uint64_t val = 0;
  ASSERT_TRUE(process_event(str, perfoptions_lookup(), perfoptions_nb_presets(),
                            &i, &val));
  ASSERT_EQ(i, 0);
  ASSERT_EQ(val, 555); // value changed
}

// An event without a separator is invalid, even if the components are valid.
// This is because we may wish to have event types which end in a number at some
// point.
TEST(CmdLineTst, LiteralEventWithNoComma) {
  char const *str = "hCPU1";
  size_t i = 999999;
  uint64_t v = 0;
  ASSERT_FALSE(process_event(str, perfoptions_lookup(),
                             perfoptions_nb_presets(), &i, &v));
}

TEST(CmdLineTst, LiteralEventWithVeryBadValue) {
  char const *str = "hCPU,apples";
  size_t i = 999999;
  uint64_t v = 1;
  ASSERT_FALSE(process_event(str, perfoptions_lookup(),
                             perfoptions_nb_presets(), &i, &v));
}

TEST(CmdLineTst, LiteralEventWithKindaBadValue) {
  char const *str = "hCPU,123apples";
  size_t i = 999999;
  uint64_t v = 1;
  ASSERT_FALSE(process_event(str, perfoptions_lookup(),
                             perfoptions_nb_presets(), &i, &v));
}
