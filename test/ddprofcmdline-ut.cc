// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_cmdline.hpp"
#include "ddprof_cmdline_watcher.hpp"
#include "perf_archmap.hpp"
#include "perf_watcher.hpp"

#include <gtest/gtest.h>

namespace ddprof {
namespace {
const std::string_view sTestPaterns[] = {"cAn", "yUo", "eVen", "tYpe"};

bool watcher_from_str(const char *s, PerfWatcher *watcher) {
  std::vector<PerfWatcher> watchers;
  bool res = watchers_from_str(s, watchers);
  if (!res) {
    return false;
  }
  EXPECT_EQ(watchers.size(), 1);
  *watcher = watchers.front();
  return true;
}
} // namespace

TEST(CmdLineTst, ArgWhich) {
  ASSERT_EQ(arg_which("tYpe", sTestPaterns), 3);
  ASSERT_EQ(arg_which("type", sTestPaterns), 3);
  ASSERT_EQ(arg_which("typo", sTestPaterns), -1);
}

TEST(CmdLineTst, ArgYesNo) {
  const char *yesStr = "YeS";
  const char *noStr = "nO";
  ASSERT_TRUE(arg_yes(yesStr));
  ASSERT_FALSE(arg_yes(noStr));
  ASSERT_TRUE(arg_no(noStr));
  ASSERT_FALSE(arg_no(yesStr));
}

TEST(CmdLineTst, PartialFilled) {
  std::string_view partialPaterns[] = {"cAn", "temp", "eVen", "tYpe"};
  ASSERT_EQ(arg_which("temp", partialPaterns), 1);
  partialPaterns[1] = {};
  ASSERT_EQ(arg_which("typo", partialPaterns),
            -1); // Check that we can iterate safely over everything
}

TEST(CmdLineTst, NullPatterns) {
  ASSERT_EQ(arg_which("typo", {}),
            -1); // Check that we can iterate safely over everything
}

TEST(CmdLineTst, FirstEventHit) {
  char const *str = "hCPU";
  PerfWatcher watcher;
  ASSERT_TRUE(watcher_from_str(str, &watcher));
  ASSERT_EQ(watcher.type, PERF_TYPE_HARDWARE);
  ASSERT_EQ(watcher.type, PERF_COUNT_HW_CPU_CYCLES);
}

TEST(CmdLineTst, ParserKeyPatterns) {
  PerfWatcher watcher;

  // Simple events without qualification are valid event names
  ASSERT_TRUE(watcher_from_str("hCPU", &watcher));

  // Events should be tolerant of padding whitespace
  // Three checks on each side to ensure fully recursive (base, 1, 2) stripping
  ASSERT_TRUE(watcher_from_str(" hCPU", &watcher));
  ASSERT_TRUE(watcher_from_str("  hCPU", &watcher));
  ASSERT_TRUE(watcher_from_str("   hCPU", &watcher));
  ASSERT_TRUE(watcher_from_str("hCPU ", &watcher));
  ASSERT_TRUE(watcher_from_str("hCPU  ", &watcher));
  ASSERT_TRUE(watcher_from_str("hCPU   ", &watcher));
  ASSERT_TRUE(watcher_from_str("   hCPU   ", &watcher));

  // Checking only the two-sided fully recursive whitespace stripping for
  // value is sufficient?
  ASSERT_EQ(watcher.type, PERF_TYPE_HARDWARE);
  ASSERT_EQ(watcher.config, PERF_COUNT_HW_CPU_CYCLES);

  // Extended events: e|event|eventname
  ASSERT_TRUE(watcher_from_str("eventname=hCPU", &watcher));
  ASSERT_TRUE(watcher_from_str("event=hCPU", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU", &watcher));
  ASSERT_EQ(watcher.type, PERF_TYPE_HARDWARE);
  ASSERT_EQ(watcher.config, PERF_COUNT_HW_CPU_CYCLES);

  // Extended events are also whitespace insensitive
  ASSERT_TRUE(watcher_from_str("   e=hCPU   ", &watcher));
  ASSERT_EQ(watcher.type, PERF_TYPE_HARDWARE);
  ASSERT_EQ(watcher.config, PERF_COUNT_HW_CPU_CYCLES);

  // Events fail if invalid
  ASSERT_FALSE(watcher_from_str("invalidEvent", &watcher));
  ASSERT_FALSE(watcher_from_str("e=invalidEvent", &watcher));

  // Extended events with a group are tracepoints, and tracepoints are checked
  // against tracefs for validity.  We don't have a positive check, since that
  // assumes access to tracefs
  ASSERT_FALSE(watcher_from_str("e=invalidEvent g=group", &watcher));

  // Extended events _do_ require a valid event to be specified
  ASSERT_TRUE(watcher_from_str("e=hCPU l=myLabel", &watcher));
  ASSERT_FALSE(watcher_from_str("l=myLabel", &watcher));

  // s|arg_scale|scale
  ASSERT_TRUE(watcher_from_str("e=hCPU s=1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU value_scale=1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU scale=1", &watcher));

  // Scale be floats and/or have sign, be zero
  ASSERT_TRUE(watcher_from_str("e=hCPU s=1.0", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU s=+1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU s=-1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU s=+1.0", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU s=-1.0", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU s=0", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU s=+0", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU s=-0", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU s=+0.0", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU s=-0.0", &watcher));

  // but it is too weird for scale to be given in hex?
  ASSERT_FALSE(watcher_from_str("e=hCPU s=0x0f", &watcher));

  // Floats can't be exponentials
  ASSERT_FALSE(watcher_from_str("e=hCPU s=1e1", &watcher));

  // f|frequency|freq
  ASSERT_TRUE(watcher_from_str("e=hCPU f=1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU freq=1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU frequency=1", &watcher));

  // p|period|per
  // FIXME periods should never be negative, but we allow it for the
  // allocation profiler
  ASSERT_TRUE(watcher_from_str("e=hCPU p=1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU per=1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU period=1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU period=-1", &watcher));

  // period + frequency is ambiguous, failure
  ASSERT_FALSE(watcher_from_str("e=hCPU p=1 f=1", &watcher));

  // l|label
  ASSERT_TRUE(watcher_from_str("e=hCPU l=foo", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU label=foo", &watcher));

  // Labels can contain numbers
  ASSERT_TRUE(watcher_from_str("e=hCPU label=foo123", &watcher));

  // Labels ("words") cannot start with numbers
  ASSERT_FALSE(watcher_from_str("e=hCPU label=14b31", &watcher));

  // Labels cannot _be_ numbers
  ASSERT_FALSE(watcher_from_str("e=hCPU label=14631", &watcher));

  // m|mode
  ASSERT_FALSE(watcher_from_str("e=hCPU m=g", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU m=s", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU mode=s", &watcher));

  // Mode is not permissive
  ASSERT_FALSE(watcher_from_str("e=hCPU mode=magnanimous", &watcher));

  // A or a designate all
  ASSERT_TRUE(watcher_from_str("e=hCPU mode=A", &watcher));
  ASSERT_TRUE(Any(watcher.aggregation_mode & EventAggregationMode::kSum));
  ASSERT_TRUE(watcher_from_str("e=hCPU mode=a", &watcher));
  ASSERT_TRUE(Any(watcher.aggregation_mode &
                  EventAggregationMode::kSum)); // watcher.output_mode <=
                                                // EventConfMode::kCallgraph

  // both m and g together designate all
  ASSERT_TRUE(watcher_from_str("e=hCPU mode=SL", &watcher));
  ASSERT_TRUE(Any(watcher.aggregation_mode &
                  EventAggregationMode::kSum)); // watcher.output_mode <=
                                                // EventConfMode::kCallgraph
  ASSERT_TRUE(watcher_from_str("e=hCPU mode=sl", &watcher));
  EXPECT_TRUE(Any(watcher.aggregation_mode & EventAggregationMode::kLiveSum));
  EXPECT_TRUE(Any(watcher.aggregation_mode & EventAggregationMode::kSum));
  ASSERT_TRUE(Any(watcher.aggregation_mode &
                  EventAggregationMode::kSum)); // watcher.output_mode <=
                                                // EventConfMode::kCallgraph

  // M or m is a metric (no callgraph unless specified)
  ASSERT_TRUE(watcher_from_str("e=hCPU mode=s", &watcher));
  ASSERT_TRUE(Any(watcher.aggregation_mode &
                  EventAggregationMode::kSum)); // watcher.output_mode <=
                                                // EventConfMode::kCallgraph
  ASSERT_TRUE(watcher_from_str("e=hCPU mode=S", &watcher));
  ASSERT_TRUE(Any(watcher.aggregation_mode &
                  EventAggregationMode::kSum)); // watcher.output_mode <=
                                                // EventConfMode::kCallgraph

  // G or g designate callgraph (default)
  ASSERT_TRUE(watcher_from_str("e=hCPU", &watcher));
  ASSERT_TRUE(Any(watcher.aggregation_mode &
                  EventAggregationMode::kSum)); // watcher.output_mode <=
                                                // EventConfMode::kCallgraph
  // n|arg_num|argno
  ASSERT_TRUE(watcher_from_str("e=hCPU n=1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU argno=1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU arg_num=1", &watcher));

  // argno should expand the given number into the correct sys-V register
  // for the given 1-indexed parameter value
  ASSERT_TRUE(watcher_from_str("e=hCPU n=1", &watcher));
  ASSERT_EQ(watcher.regno, param_to_perf_regno(1));
  ASSERT_TRUE(watcher_from_str("e=hCPU n=2", &watcher));
  ASSERT_EQ(watcher.regno, param_to_perf_regno(2));
  ASSERT_TRUE(watcher_from_str("e=hCPU n=3", &watcher));
  ASSERT_EQ(watcher.regno, param_to_perf_regno(3));

  // 0-parameter is an error
  ASSERT_FALSE(watcher_from_str("e=hCPU n=0", &watcher));

  // argno should be bounds-checked
  ASSERT_FALSE(watcher_from_str("e=hCPU n=100", &watcher));

  // argno can only be a uint
  ASSERT_FALSE(watcher_from_str("e=hCPU n=1.0", &watcher));
  ASSERT_FALSE(watcher_from_str("e=hCPU n=-1", &watcher));
  ASSERT_FALSE(watcher_from_str("e=hCPU n=rax", &watcher));

  // ... but it CAN be a hex uint because all uints can be
  ASSERT_TRUE(watcher_from_str("e=hCPU n=0x01", &watcher));

  // o|raw_offset|rawoff
  ASSERT_TRUE(watcher_from_str("e=hCPU o=0", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU rawoff=0", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU raw_offset=0", &watcher));

  // rawoff is a uint.  If it has an upper bound, I don't know what it is yet.
  ASSERT_FALSE(watcher_from_str("e=hCPU o=1.0", &watcher));
  ASSERT_FALSE(watcher_from_str("e=hCPU o=-1", &watcher));
  ASSERT_FALSE(watcher_from_str("e=hCPU o=rax", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU o=0x0", &watcher));

  // p|period|per
  ASSERT_TRUE(watcher_from_str("e=hCPU p=1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU per=1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU period=1", &watcher));

  // Period is a uint.
  // FIXME temporarily relaxing this
  ASSERT_FALSE(watcher_from_str("e=hCPU p=1.0", &watcher));
  //  ASSERT_FALSE(watcher_from_str("e=hCPU p=-1", &watcher));
  ASSERT_FALSE(watcher_from_str("e=hCPU p=lots", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU p=0x0", &watcher));

  // r|register|regno
  ASSERT_TRUE(watcher_from_str("e=hCPU r=1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU regno=1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU register=1", &watcher));

  // Right now the register is the linux/perf register number, which can be 0
  ASSERT_TRUE(watcher_from_str("e=hCPU r=0", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU r=0x1", &watcher));

  // ... but is still bounded by the architecture
  ASSERT_FALSE(watcher_from_str("e=hCPU r=100", &watcher));

  // z|raw_size|rawsz
  ASSERT_TRUE(watcher_from_str("e=hCPU z=4", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU rawsz=4", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU raw_size=4", &watcher));

  // Check for allowed integer sizes
  ASSERT_FALSE(watcher_from_str("e=hCPU z=-1", &watcher));
  ASSERT_FALSE(watcher_from_str("e=hCPU z=0", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU z=1", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU z=2", &watcher));
  ASSERT_FALSE(watcher_from_str("e=hCPU z=3", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU z=4", &watcher));
  ASSERT_FALSE(watcher_from_str("e=hCPU z=5", &watcher));
  ASSERT_FALSE(watcher_from_str("e=hCPU z=6", &watcher));
  ASSERT_FALSE(watcher_from_str("e=hCPU z=7", &watcher));
  ASSERT_TRUE(watcher_from_str("e=hCPU z=8", &watcher));
  ASSERT_FALSE(watcher_from_str("e=hCPU z=9", &watcher));
  ASSERT_FALSE(watcher_from_str("e=hCPU z=16", &watcher));
  ASSERT_FALSE(watcher_from_str("e=hCPU z=32", &watcher));
  ASSERT_FALSE(watcher_from_str("e=hCPU z=64", &watcher));
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
  char const *str = "event=hCPU period=555";
  PerfWatcher watcher = {};
  watcher.sample_period = 12345;
  ASSERT_TRUE(watcher_from_str(str, &watcher));
  ASSERT_EQ(watcher.sample_period, 555); // value changed
}

// An event without a separator is invalid, even if the components are valid.
// This is because we may wish to have event types which end in a number at some
// point.
TEST(CmdLineTst, LiteralEventWithNoComma) {
  char const *str = "hCPU1";
  PerfWatcher watcher = {};
  ASSERT_FALSE(watcher_from_str(str, &watcher));
}

TEST(CmdLineTst, LiteralEventWithVeryBadValue) {
  char const *str = "hCPU period=apples";
  PerfWatcher watcher = {};
  ASSERT_FALSE(watcher_from_str(str, &watcher));
}

TEST(CmdLineTst, LiteralEventWithRedundantSettings) {
  char const *str = "hCPU mode=l mode=a";
  PerfWatcher watcher = {};
  // todo this parsing should not be OK
  ASSERT_TRUE(watcher_from_str(str, &watcher));
}

TEST(CmdLineTst, LiteralEventWithKindaBadValue) {
  char const *str = "hCPU period=123apples";
  PerfWatcher watcher = {};
  ASSERT_FALSE(watcher_from_str(str, &watcher));
}

TEST(CmdLineTst, EmptyConfigs) {
  char const *str = "; sCPU   ; ;;;; ;;; ;;";
  std::vector<PerfWatcher> watchers;
  ASSERT_TRUE(watchers_from_str(str, watchers));
  ASSERT_EQ(watchers.size(), 1);
}

TEST(CmdLineTst, MultipleEvents) {
  char const *str = "; sCPU   ; sALLOC ;;;; ;;; ;;";
  std::vector<PerfWatcher> watchers;
  ASSERT_TRUE(watchers_from_str(str, watchers));
  ASSERT_EQ(watchers.size(), 2);
}
} // namespace ddprof
