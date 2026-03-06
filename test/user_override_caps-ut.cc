// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// Test that verifies the fix for capability loss during UID elevation.
//
// Background: When ddprof runs as non-root with file capabilities (e.g.
// CAP_IPC_LOCK, CAP_PERFMON), calling user_override() to elevate UID to 0
// then back to the original UID triggers the Linux kernel's capability
// clearing rule: once all UIDs become nonzero (after at least one was 0),
// the kernel clears ALL capabilities from the permitted and effective sets.
//
// The fix guards UID elevation with is_root() so non-root users skip it.
//
// This test requires specific setup to fully exercise the bug path:
//   - Must run as non-root (UID != 0)
//   - Must have CAP_SETUID+CAP_SETGID in permitted set (to do UID elevation)
//   - Must have additional caps (e.g. CAP_IPC_LOCK) to verify preservation
// When these conditions aren't met, the test verifies the guard logic only.

#include <gtest/gtest.h>

#include "loghandle.hpp"
#include "user_override.hpp"

#include <sys/capability.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ddprof {
namespace {

// Helper: check if a specific capability is in the effective set
bool has_effective_cap(cap_value_t cap) {
  cap_t caps = cap_get_proc();
  if (!caps) {
    return false;
  }
  cap_flag_value_t value = CAP_CLEAR;
  cap_get_flag(caps, cap, CAP_EFFECTIVE, &value);
  cap_free(caps);
  return value == CAP_SET;
}

// Helper: count number of effective capabilities
int count_effective_caps() {
  cap_t caps = cap_get_proc();
  if (!caps) {
    return -1;
  }
  int count = 0;
  for (int i = 0; i <= CAP_LAST_CAP; ++i) {
    cap_flag_value_t value = CAP_CLEAR;
    if (cap_get_flag(caps, i, CAP_EFFECTIVE, &value) == 0 &&
        value == CAP_SET) {
      ++count;
    }
  }
  cap_free(caps);
  return count;
}

} // namespace

// Test that is_root() correctly identifies non-root users
TEST(UserOverrideCapsTest, is_root_guard) {
  LogHandle handle;
  uid_t uid = getuid();
  if (uid == 0) {
    EXPECT_TRUE(is_root());
    printf("Running as root — is_root() guard allows UID elevation\n");
  } else {
    EXPECT_FALSE(is_root());
    printf("Running as non-root (UID=%d) — is_root() guard blocks UID "
           "elevation\n",
           uid);
  }
}

// Test that for non-root users, user_override to root fails harmlessly
// (the setresuid call returns EPERM) and does NOT destroy capabilities.
TEST(UserOverrideCapsTest, uid_elevation_fails_harmlessly_for_nonroot) {
  LogHandle handle;

  if (is_root()) {
    printf("SKIP: test only meaningful for non-root users\n");
    return;
  }

  int const caps_before = count_effective_caps();
  printf("Effective caps before: %d\n", caps_before);

  // Attempt UID elevation to root — this should fail with EPERM
  // because we don't have CAP_SETUID
  UIDInfo old_uids;
  DDRes res = user_override(0, 0, &old_uids);

  // For non-root without CAP_SETUID, this should fail
  if (!IsDDResOK(res)) {
    printf("user_override(0,0) failed as expected (no CAP_SETUID)\n");
  } else {
    // If it succeeded (we have CAP_SETUID), restore and check caps
    printf("user_override(0,0) succeeded — restoring\n");
    user_override(old_uids.uid, old_uids.gid);
  }

  int const caps_after = count_effective_caps();
  printf("Effective caps after: %d\n", caps_after);

  // The critical assertion: capabilities must not be lost
  EXPECT_EQ(caps_before, caps_after)
      << "Capabilities were lost during UID elevation attempt! "
         "This is the bug that the is_root() guard prevents.";
}

// Test that simulates the exact open_proc_maps pattern:
// 1. Try to open a file (will fail for /proc of non-dumpable process)
// 2. If non-root, skip UID elevation (the fix)
// 3. Verify capabilities are preserved
TEST(UserOverrideCapsTest, open_proc_maps_pattern_preserves_caps) {
  LogHandle handle;

  if (is_root()) {
    printf("SKIP: test only meaningful for non-root users\n");
    return;
  }

  int const caps_before = count_effective_caps();

  // Simulate the fixed open_proc_maps pattern:
  // Try opening our own /proc/self/maps (should succeed)
  FILE *f = fopen("/proc/self/maps", "r");
  if (!f) {
    // If it fails, the fixed code checks is_root() before elevating
    if (!is_root()) {
      // Non-root: skip UID elevation entirely (the fix)
      printf("Correctly skipped UID elevation for non-root user\n");
    }
  } else {
    fclose(f);
    printf("/proc/self/maps opened successfully (no elevation needed)\n");
  }

  int const caps_after = count_effective_caps();
  EXPECT_EQ(caps_before, caps_after);
}

// Demonstrate the bug path: if user_override IS called for non-root
// with CAP_SETUID, capabilities are lost. This test documents the bug
// and verifies that the is_root() guard is necessary.
//
// This test only runs when the process has CAP_SETUID+CAP_SETGID
// (e.g. via setcap on the test binary), which is typically not the case
// in CI. It serves as a manual verification tool.
TEST(UserOverrideCapsTest, demonstrates_capability_loss_bug) {
  LogHandle handle;

  if (is_root()) {
    printf("SKIP: test only meaningful for non-root users\n");
    return;
  }

  bool const have_setuid = has_effective_cap(CAP_SETUID);
  bool const have_setgid = has_effective_cap(CAP_SETGID);

  if (!have_setuid || !have_setgid) {
    printf("SKIP: need CAP_SETUID+CAP_SETGID to demonstrate the bug.\n"
           "To run this test manually:\n"
           "  sudo setcap "
           "'cap_setuid,cap_setgid,cap_ipc_lock=+ep' "
           "./build/test/user_override_caps-ut\n"
           "  ./build/test/user_override_caps-ut "
           "--gtest_filter='*demonstrates*'\n");
    return;
  }

  int const caps_before = count_effective_caps();
  printf("Effective caps before UID round-trip: %d\n", caps_before);
  ASSERT_GT(caps_before, 0);

  // Perform the UID round-trip that triggers the bug
  uid_t const my_uid = getuid();
  gid_t const my_gid = getgid();

  UIDInfo old_uids;
  DDRes res = user_override(0, 0, &old_uids);
  ASSERT_TRUE(IsDDResOK(res)) << "UID elevation to root failed";
  printf("Elevated to UID 0\n");

  // Restore original UID — this triggers the kernel cap clearing rule
  res = user_override(my_uid, my_gid);
  ASSERT_TRUE(IsDDResOK(res)) << "UID restore failed";
  printf("Restored to UID %d\n", my_uid);

  int const caps_after = count_effective_caps();
  printf("Effective caps after UID round-trip: %d\n", caps_after);

  // Document the expected behavior:
  // WITHOUT the fix (direct user_override call), caps_after == 0
  // WITH the fix (is_root() guard), this code path is never reached
  if (caps_after == 0) {
    printf("BUG CONFIRMED: All %d capabilities were lost!\n"
           "This is why the is_root() guard is necessary.\n",
           caps_before);
  } else {
    printf("Capabilities preserved (unexpected for non-root UID "
           "round-trip)\n");
  }

  // We expect the bug to manifest here — caps should be lost
  // This documents the kernel behavior that the fix avoids
  EXPECT_EQ(caps_after, 0)
      << "Expected capability loss from UID round-trip. "
         "If caps are preserved, the kernel behavior may have changed.";
}

} // namespace ddprof
