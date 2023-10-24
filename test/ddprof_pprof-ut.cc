// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "pprof/ddprof_pprof.hpp"

#include "ddog_profiling_utils.hpp"
#include "ddprof_cmdline.hpp"
#include "ddprof_cmdline_watcher.hpp"
#include "loghandle.hpp"
#include "pevent_lib_mocks.hpp"
#include "symbol_hdr.hpp"
#include "unwind_output_mock.hpp"

#include <cstdlib>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <string>
#include <time.h>
#include <unistd.h>

namespace ddprof {
// todo : cut this dependency
DwflSymbolLookup::DwflSymbolLookup() : _lookup_setting(K_CACHE_ON) {}

TEST(DDProfPProf, init_profiles) {
  DDProfPProf pprof;
  DDProfContext ctx = {};
  ctx.watchers.push_back(*ewatcher_from_str("sCPU"));
  DDRes res = pprof_create_profile(&pprof, ctx);
  EXPECT_TRUE(IsDDResOK(res));
  res = pprof_free_profile(&pprof);
  EXPECT_TRUE(IsDDResOK(res));
}

void test_pprof(DDProfPProf *pprofs) {
  ddog_prof_Profile *profile = &pprofs->_profile;

  struct ddog_prof_Profile_SerializeResult serialized_result =
      ddog_prof_Profile_serialize(profile, nullptr, nullptr, nullptr);

  ASSERT_EQ(serialized_result.tag, DDOG_PROF_PROFILE_SERIALIZE_RESULT_OK);

  ddog_Timespec start = serialized_result.ok.start;

  // Check that encoded time is close to now
  time_t local_time = time(NULL);
  EXPECT_TRUE(local_time - start.seconds < 2);

  ddog_Vec_U8 profile_vec = serialized_result.ok.buffer;

  EXPECT_TRUE(profile_vec.ptr);

  // Test that we are generating content
  EXPECT_TRUE(profile_vec.len > 500);
  EXPECT_TRUE(profile_vec.capacity >= profile_vec.len);

  ddog_prof_EncodedProfile_drop(&serialized_result.ok);
}

TEST(DDProfPProf, aggregate) {
  LogHandle handle;
  SymbolHdr symbol_hdr;
  UnwindOutput mock_output;
  SymbolTable &table = symbol_hdr._symbol_table;
  MapInfoTable &mapinfo_table = symbol_hdr._mapinfo_table;

  fill_unwind_symbols(table, mapinfo_table, mock_output);
  DDProfPProf pprof;
  DDProfContext ctx = {};

  bool ok = watchers_from_str("sCPU", ctx.watchers);
  EXPECT_TRUE(ok);
  DDRes res = pprof_create_profile(&pprof, ctx);
  EXPECT_TRUE(ctx.watchers[0].pprof_indices[kSumPos].pprof_index != -1);
  EXPECT_TRUE(ctx.watchers[0].pprof_indices[kSumPos].pprof_count_index != -1);
  res = pprof_aggregate(&mock_output, symbol_hdr, {1000, 1, 0},
                        &ctx.watchers[0], kSumPos, &pprof);

  EXPECT_TRUE(IsDDResOK(res));

  test_pprof(&pprof);

  res = pprof_free_profile(&pprof);
  EXPECT_TRUE(IsDDResOK(res));
}

TEST(DDProfPProf, just_live) {
  LogHandle handle;
  SymbolHdr symbol_hdr;
  UnwindOutput mock_output;
  SymbolTable &table = symbol_hdr._symbol_table;
  MapInfoTable &mapinfo_table = symbol_hdr._mapinfo_table;

  fill_unwind_symbols(table, mapinfo_table, mock_output);
  DDProfPProf pprof;
  DDProfContext ctx = {};
  {
    bool ok = watchers_from_str("sDUM", ctx.watchers);
    EXPECT_TRUE(ok);
  }
  {
    bool ok = watchers_from_str("sALLOC mode=l", ctx.watchers);
    EXPECT_TRUE(ok);
  }
  log_watcher(&(ctx.watchers[0]), 0);
  log_watcher(&(ctx.watchers[1]), 1);

  DDRes res = pprof_create_profile(&pprof, ctx);
  EXPECT_TRUE(ctx.watchers[0].pprof_indices[kSumPos].pprof_index == -1);
  EXPECT_TRUE(ctx.watchers[0].pprof_indices[kSumPos].pprof_count_index == -1);

  EXPECT_TRUE(ctx.watchers[1].pprof_indices[kLiveSumPos].pprof_index != -1);
  EXPECT_TRUE(ctx.watchers[1].pprof_indices[kLiveSumPos].pprof_count_index !=
              -1);
  res = pprof_aggregate(&mock_output, symbol_hdr, {1000, 1, 0},
                        &ctx.watchers[1], kLiveSumPos, &pprof);
  EXPECT_TRUE(IsDDResOK(res));
  test_pprof(&pprof);
  res = pprof_free_profile(&pprof);
  EXPECT_TRUE(IsDDResOK(res));
}

} // namespace ddprof
