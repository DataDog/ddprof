extern "C" {
#include "pprof/ddprof_pprof.h"

#include "perf_option.h"
#include "unwind_output_mock.h"

#include <ddprof/ffi.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
}
#include "loghandle.hpp"

#include <cstdlib>
#include <gtest/gtest.h>
#include <string>

TEST(DDProfPProf, init_profiles) {
  DDProfPProf pprofs;
  const PerfOption *perf_option_cpu = perfoptions_preset(10);
  DDRes res = pprof_create_profile(&pprofs, perf_option_cpu, 1);
  EXPECT_TRUE(IsDDResOK(res));
  res = pprof_free_profile(&pprofs);
  EXPECT_TRUE(IsDDResOK(res));
}

void test_pprof(const DDProfPProf *pprofs) {
  const ddprof_ffi_Profile *profile = pprofs->_profile;
  struct ddprof_ffi_EncodedProfile *encoded_profile =
      ddprof_ffi_Profile_serialize(profile);

  EXPECT_TRUE(encoded_profile);

  ddprof_ffi_Timespec start = encoded_profile->start;

  // Check that encoded time is close to now
  time_t local_time = time(NULL);
  EXPECT_TRUE(local_time - start.seconds < 1);

  ddprof_ffi_Buffer profile_buffer = {
      .ptr = encoded_profile->buffer.ptr,
      .len = encoded_profile->buffer.len,
      .capacity = encoded_profile->buffer.capacity,
  };
  EXPECT_TRUE(profile_buffer.ptr);

  // Test that we are generating content
  EXPECT_TRUE(profile_buffer.len > 500);
  EXPECT_TRUE(profile_buffer.capacity >= profile_buffer.len);
  ddprof_ffi_EncodedProfile_delete(encoded_profile);
}

TEST(DDProfPProf, aggregate) {
  LogHandle handle;
  UnwindOutput mock_output;
  fill_unwind_output_1(&mock_output);
  DDProfPProf pprofs;
  const PerfOption *perf_option_cpu = perfoptions_preset(10);

  DDRes res = pprof_create_profile(&pprofs, perf_option_cpu, 1);
  EXPECT_TRUE(IsDDResOK(res));

  res = pprof_aggregate(&mock_output, 1000, 0, &pprofs);
  EXPECT_TRUE(IsDDResOK(res));

  test_pprof(&pprofs);

  res = pprof_free_profile(&pprofs);
  EXPECT_TRUE(IsDDResOK(res));
}
