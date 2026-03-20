// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// These tests cover the temporary procfs owner switch used by
// `open_proc_maps()` and `open_proc_comm()`.
//
// The useful cases are:
// - root can temporarily switch to the proc entry owner
// - non-root can still temporarily switch to another non-zero owner
//
// The unsafe case is a non-root -> 0 -> non-root round-trip, which clears the
// process capability sets. Fully reproducing that needs a non-root process that
// also holds CAP_SETUID and CAP_SETGID, which is uncommon in CI.
//
// How to reproduce in a privileged container
// ------------------------------------------
// /app, /home, /tmp are nosuid in Docker — file capabilities are silently
// ignored there. Copy the binary to /usr/local/bin (no nosuid).
//
//   # From the repo root, start a container with the required caps:
//   ./tools/launch_local_build.sh -u 24 --cap-test
//
//   # Inside the container (running as root):
//   BUILD=build_gcc_unknown-linux-2.39_DebTidy
//   make -C /app/$BUILD user_override_caps-ut -j$(nproc)
//   cp /app/$BUILD/test/user_override_caps-ut /usr/local/bin/
//   setcap 'cap_setuid,cap_setgid,cap_ipc_lock=+ep'
//   /usr/local/bin/user_override_caps-ut
//   setpriv --reuid=ubuntu --regid=ubuntu --clear-groups \
//     -- /usr/local/bin/user_override_caps-ut

#include <gtest/gtest.h>

#include "loghandle.hpp"
#include "user_override.hpp"

#include <pwd.h>
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
    if (cap_get_flag(caps, i, CAP_EFFECTIVE, &value) == 0 && value == CAP_SET) {
      ++count;
    }
  }
  cap_free(caps);
  return count;
}

} // namespace

// Test that for non-root users, user_override to root fails harmlessly
// (the setresuid call returns EPERM) and does NOT destroy capabilities.
TEST(UserOverrideCapsTest, uid_elevation_fails_harmlessly_for_nonroot) {
  LogHandle handle;

  if (is_root()) {
    printf("SKIP: test only meaningful for non-root users\n");
    return;
  }

  if (has_effective_cap(CAP_SETUID)) {
    // With CAP_SETUID the round-trip to UID 0 would succeed and deliberately
    // destroy capabilities — that scenario is covered by
    // demonstrates_capability_loss_bug.
    printf("SKIP: has CAP_SETUID — see demonstrates_capability_loss_bug\n");
    return;
  }

  int const caps_before = count_effective_caps();
  printf("Effective caps before: %d\n", caps_before);

  // Attempt UID elevation to root — this should fail with EPERM
  // because we don't have CAP_SETUID
  UIDInfo old_uids;
  DDRes res = user_override(0, 0, &old_uids);

  if (!IsDDResOK(res)) {
    printf("user_override(0,0) failed as expected (no CAP_SETUID)\n");
  } else {
    printf("user_override(0,0) succeeded — restoring\n");
    user_override(old_uids.uid, old_uids.gid);
  }

  int const caps_after = count_effective_caps();
  printf("Effective caps after: %d\n", caps_after);

  EXPECT_EQ(caps_before, caps_after)
      << "Capabilities were lost during UID elevation attempt! "
         "This is the bug that the is_root() guard prevents.";
}

// Test that a non-root -> non-zero UID -> non-root round-trip preserves caps.
// This is the safe path enabled by the (is_root() || info.st_uid != 0) guard:
// switching between two non-zero UIDs never triggers the kernel's capability-
// clearing rule. Without CAP_SETUID the switch will fail harmlessly (EPERM),
// but caps must still be intact afterwards.
TEST(UserOverrideCapsTest, nonroot_nonzero_uid_roundtrip_preserves_caps) {
  LogHandle handle;

  if (is_root()) {
    printf("SKIP: test only meaningful for non-root users\n");
    return;
  }

  // Find a stable non-zero UID to switch to ("nobody" or the well-known 65534)
  struct passwd *pw = getpwnam("nobody");
  uid_t const target_uid = pw ? pw->pw_uid : 65534;
  gid_t const target_gid = pw ? pw->pw_gid : 65534;

  if (target_uid == 0 || target_gid == 0) {
    printf("SKIP: 'nobody' maps to UID/GID 0 on this system\n");
    return;
  }

  uid_t const my_uid = getuid();
  if (target_uid == my_uid) {
    printf("SKIP: already running as the target UID %d\n", target_uid);
    return;
  }

  int const caps_before = count_effective_caps();
  printf("Effective caps before non-zero UID round-trip: %d\n", caps_before);

  UIDInfo old_uids;
  DDRes res = user_override(target_uid, target_gid, &old_uids);

  if (!IsDDResOK(res)) {
    // Without CAP_SETUID this is expected — verify no cap damage
    printf("user_override(%d,%d) failed as expected (no CAP_SETUID)\n",
           target_uid, target_gid);
  } else {
    printf("Switched to UID %d, restoring to UID %d\n", target_uid, my_uid);
    user_override(old_uids.uid, old_uids.gid);
    printf("Restored\n");
  }

  int const caps_after = count_effective_caps();
  printf("Effective caps after: %d\n", caps_after);

  // A non-zero UID round-trip must never destroy capabilities, whether the
  // switch succeeded or failed.
  EXPECT_EQ(caps_before, caps_after)
      << "Capabilities were lost during non-zero UID round-trip! "
         "Only a UID-0 round-trip should trigger the kernel cap-clearing rule.";
}

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
           "This reproduces the historical non-root -> 0 -> non-root path\n"
           "that procfs retries must avoid.\n"
           "To run this test manually (binary must live on a non-nosuid fs):\n"
           "  cp ./build/test/user_override_caps-ut /usr/local/bin/\n"
           "  sudo setcap "
           "'cap_setuid,cap_setgid,cap_ipc_lock=+ep' "
           "/usr/local/bin/user_override_caps-ut\n"
           "  /usr/local/bin/user_override_caps-ut "
           "--gtest_filter='*demonstrates*'\n"
           "Note: /home and /tmp are typically mounted nosuid, which "
           "silently prevents file capabilities from taking effect.\n");
    return;
  }

  int const caps_before = count_effective_caps();
  printf("Effective caps before UID round-trip: %d\n", caps_before);
  ASSERT_GT(caps_before, 0);

  uid_t const my_uid = getuid();
  gid_t const my_gid = getgid();

  UIDInfo old_uids;
  DDRes res = user_override(0, 0, &old_uids);
  ASSERT_TRUE(IsDDResOK(res)) << "UID elevation to root failed";
  printf("Elevated to UID 0\n");

  res = user_override(my_uid, my_gid);
  ASSERT_TRUE(IsDDResOK(res)) << "UID restore failed";
  printf("Restored to UID %d\n", my_uid);

  int const caps_after = count_effective_caps();
  printf("Effective caps after UID round-trip: %d\n", caps_after);

  if (caps_after == 0) {
    printf("BUG CONFIRMED: All %d capabilities were lost!\n"
           "This is why the is_root() guard is necessary.\n",
           caps_before);
  } else {
    printf("Capabilities preserved (unexpected for non-root UID "
           "round-trip)\n");
  }

  EXPECT_EQ(caps_after, 0)
      << "Expected capability loss from UID round-trip. "
         "If caps are preserved, the kernel behavior may have changed.";
}

} // namespace ddprof
