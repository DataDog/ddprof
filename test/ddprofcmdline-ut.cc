extern "C" {
#include "ddprofcmdline.h"
#include "perf.h" // perfoptions_lookup
}

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
  char const *str = perfoptions_lookup[0];
  size_t i = 999999;
  uint64_t val = 0;
  ASSERT_TRUE(process_event(str, perfoptions_lookup, perfoptions_sz, &i, &val));
  ASSERT_EQ(i, 0);
}

TEST(CmdLineTst, LastEventHit) {
  char const *str = perfoptions_lookup[perfoptions_sz - 1];
  size_t i = 999999;
  uint64_t val = 0;
  ASSERT_TRUE(process_event(str, perfoptions_lookup, perfoptions_sz, &i, &val));
  ASSERT_EQ(i, perfoptions_sz - 1);
}

TEST(CmdLineTst, LiteralEventWithGoodValue) {
  char const *str = "hCPU,555";
  size_t i = 999999;
  uint64_t val = 0;
  ASSERT_TRUE(process_event(str, perfoptions_lookup, perfoptions_sz, &i, &val));
  ASSERT_EQ(i, 0);
  ASSERT_EQ(val, 555); // value changed
}

TEST(CmdLineTst, LiteralEventWithBadValue) {
  char const *str = "hCPU,apples";
  size_t i = 999999;
  uint64_t val = 1;
  ASSERT_TRUE(process_event(str, perfoptions_lookup, perfoptions_sz, &i, &val));
  ASSERT_EQ(i, 0);
  ASSERT_EQ(val, 1); // value unchanged
}
