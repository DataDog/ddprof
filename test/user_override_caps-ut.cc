// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "loghandle.hpp"
#include "user_override.hpp"

#include <sys/capability.h>
#include <unistd.h>

namespace ddprof {
namespace {

int count_effective_caps() {
  cap_t caps = cap_get_proc();
  if (!caps) {
    return -1;
  }
  int count = 0;
  for (int i = 0; i <= CAP_LAST_CAP; ++i) {
    cap_flag_value_t value = CAP_CLEAR;
    if (cap_get_flag(caps, i, CAP_EFFECTIVE, &value) == 0 && value == CAP_SET) {
      ++count;
    }
  }
  cap_free(caps);
  return count;
}

} // namespace

// Verify that is_root() agrees with getuid().
TEST(UserOverrideCapsTest, is_root_matches_uid) {
  LogHandle handle;
  if (getuid() == 0) {
    EXPECT_TRUE(is_root());
  } else {
    EXPECT_FALSE(is_root());
  }
}

// For non-root users without CAP_SETUID, user_override(0,0) fails with
// EPERM and must not destroy existing capabilities.
TEST(UserOverrideCapsTest, failed_elevation_preserves_caps) {
  LogHandle handle;

  if (is_root()) {
    GTEST_SKIP() << "only meaningful for non-root users";
  }

  int const caps_before = count_effective_caps();

  UIDInfo old_uids;
  DDRes res = user_override(0, 0, &old_uids);

  if (IsDDResOK(res)) {
    // Unlikely in CI, but restore if it succeeded.
    user_override(old_uids.uid, old_uids.gid);
  }

  EXPECT_EQ(count_effective_caps(), caps_before)
      << "capabilities lost during failed UID elevation";
}

} // namespace ddprof
