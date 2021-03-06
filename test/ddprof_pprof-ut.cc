// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "pprof/ddprof_pprof.hpp"

#include "ddprof_ffi_utils.hpp"
#include "ddprof_input.hpp"
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
DwflSymbolLookup_V2::DwflSymbolLookup_V2() : _lookup_setting(K_CACHE_ON) {}

TEST(DDProfPProf, init_profiles) {
  DDProfPProf pprof;
  DDProfContext ctx = {};
  ctx.watchers[0] = *ewatcher_from_str("sCPU");
  ctx.num_watchers = 1;

  DDRes res = pprof_create_profile(&pprof, &ctx);
  EXPECT_TRUE(IsDDResOK(res));
  res = pprof_free_profile(&pprof);
  EXPECT_TRUE(IsDDResOK(res));
}

void test_pprof(const DDProfPProf *pprofs) {
  const ddprof_ffi_Profile *profile = pprofs->_profile;

  struct ddprof_ffi_SerializeResult serialized_result =
      ddprof_ffi_Profile_serialize(profile, nullptr, nullptr);

  ASSERT_EQ(serialized_result.tag, DDPROF_FFI_SERIALIZE_RESULT_OK);

  ddprof_ffi_Timespec start = serialized_result.ok.start;

  // Check that encoded time is close to now
  time_t local_time = time(NULL);
  EXPECT_TRUE(local_time - start.seconds < 2);

  ddprof_ffi_Vec_u8 profile_vec = serialized_result.ok.buffer;

  EXPECT_TRUE(profile_vec.ptr);

  // Test that we are generating content
  EXPECT_TRUE(profile_vec.len > 500);
  EXPECT_TRUE(profile_vec.capacity >= profile_vec.len);

  ddprof_ffi_SerializeResult_drop(serialized_result);
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
  ctx.watchers[0] = *ewatcher_from_str("sCPU");
  ctx.num_watchers = 1;
  DDRes res = pprof_create_profile(&pprof, &ctx);
  EXPECT_TRUE(IsDDResOK(res));
  res = pprof_aggregate(&mock_output, &symbol_hdr, 1000, &ctx.watchers[0],
                        &pprof);

  EXPECT_TRUE(IsDDResOK(res));

  test_pprof(&pprof);

  res = pprof_free_profile(&pprof);
  EXPECT_TRUE(IsDDResOK(res));
}

} // namespace ddprof
