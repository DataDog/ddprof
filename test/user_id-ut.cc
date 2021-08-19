#include <gtest/gtest.h>

extern "C" {
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include "user_override.h"
}

#include "loghandle.hpp"

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

// Test setuid reversion
TEST(UserIDTest, simple) {
  {
    struct passwd *pwd = getpwnam("nobody");
    uid_t ruid_old, euid_old, suid_old;
    uid_t ruid_new, euid_new, suid_new;
    getresuid(&ruid_old, &euid_old, &suid_old);

    printf("Old UIDs: R(%d) E(%d) S(%d)\n", ruid_old, euid_old, suid_old);

    // Early exit if we're not the root user
    if (0 != euid_old)
      return;

    ASSERT_TRUE(pwd != nullptr);
    uid_t uid_nobody = pwd->pw_uid;

    // Try to change the UID
    ASSERT_EQ(0, setresuid(uid_nobody, uid_nobody, -1));
    getresuid(&ruid_new, &euid_new, &suid_new);
    printf("New UIDs: R(%d) E(%d) S(%d)\n", ruid_new, euid_new, suid_new);

    // Verify we got what we asked for
    EXPECT_EQ(uid_nobody, ruid_new);
    EXPECT_EQ(uid_nobody, euid_new);
    EXPECT_EQ(suid_old, suid_new);

    // Now try to change it back
    ASSERT_EQ(0, setresuid(ruid_old, ruid_old, -1));
    getresuid(&ruid_new, &euid_new, &suid_new);
    printf("Final UIDs: R(%d) E(%d) S(%d)\n", ruid_new, euid_new, suid_new);

    // Verify we have what we started with
  }
}

TEST(UserIDTest, api) {
  EXPECT_TRUE(true);
  UIDInfo info;
  uid_t old_user = getuid();

  DDRes res = user_override(&info);
  EXPECT_TRUE(IsDDResOK(res));

  uid_t new_user = getuid();
  printf("New user = %d \n", new_user);
  if (old_user == 0) { // root
    EXPECT_TRUE(new_user != 0);
    EXPECT_TRUE(info.override);
  }
  res = revert_override(&info);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_EQ(old_user, getuid());
}
