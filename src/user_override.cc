// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include "user_override.hpp"

#include "logger.hpp"
#include "unistd.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>

static char const *const s_user_nobody = "nobody";
static const uid_t s_root_user = 0;

void init_uidinfo(UIDInfo *user_override) {
  user_override->override = false;
  user_override->previous_user = getuid();
}

static DDRes setresuid_wrapper(uid_t ruid, uid_t euid, uid_t suid) {
  int dumpable = prctl(PR_GET_DUMPABLE);
  DDRES_CHECK_INT(setresuid(ruid, euid, suid), DD_WHAT_USERID,
                  "Unable to set user %s (%s)", s_user_nobody, strerror(errno));
  // Changing the effective user id causes the dumpable attribute of the process
  // to be reset to the value of /proc/sys/fs/suid_dumpable (usually 0, cf.
  // https://man7.org/linux/man-pages/man2/prctl.2.html), which in turn makes
  // /proc/self/fd/* files unreadable by parent processes.
  // Note that this is quite strange since with dumpable
  // attribute set to 0, ownership of /proc<pid>/ is set to root, this should
  // to be an issue since we change euid only when root, but strangely doing
  // this, parent process loses the permission to read /proc/<ddprof_pid>/fd/*
  // (but not /proc/<ddprof_pid>/maps).
  // When injecting libdd_profiling.so into target process, we use
  // LD_PRELOAD=/proc/<ddprof_pid>/fd/<temp_file>, and therefore target process
  // (ie. parent process) needs to be able to read ddprof /proc/<pid>/fd/*,
  // that's why we set dumpable attribute back to its intial value at each
  // effective user id change.
  prctl(PR_SET_DUMPABLE, dumpable);
  return {};
}

DDRes user_override(UIDInfo *user_override) {
  // Inspired from
  // https://github.com/netdata/netdata/blob/71cb1ad68707718671ef57c901dfa2041f15bbe6/daemon/daemon.c#L77

  init_uidinfo(user_override);

  if (getuid() != s_root_user) {
    // Already a different user nothing to do
    return ddres_init();
  }

  struct passwd *pwd = getpwnam(s_user_nobody);
  if (!pwd) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_USERID, "Unable to find user %s",
                           s_user_nobody);
  }
  uid_t nobodyuid = pwd->pw_uid;
  DDRES_CHECK_FWD(setresuid_wrapper(nobodyuid, nobodyuid, -1));
  user_override->override = true;

  return ddres_init();
}

DDRes revert_override(UIDInfo *user_override) {
  if (!(user_override->override)) {
    // nothing to do we did not override previously
    return ddres_init();
  }
  uid_t uid_old = user_override->previous_user;
  DDRES_CHECK_FWD(setresuid_wrapper(uid_old, uid_old, -1));
  init_uidinfo(user_override);

  return ddres_init();
}

DDRes become_user(const char *username) {
  struct passwd *pw = getpwnam(username);

  if (!pw) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_USERID, "Unable to find user %s", username);
  }

  uid_t uid = pw->pw_uid;
  gid_t gid = pw->pw_gid;

  if (initgroups(pw->pw_name, gid) != 0) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_USERID, "Cannot init group list for %s",
                           username);
  }
  if (setresgid(gid, gid, gid) != 0) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_USERID,
                           "Cannot switch to user's %s group (gid: %u)",
                           username, gid);
  }
  if (setresuid(uid, uid, uid) != 0) {
    DDRES_RETURN_ERROR_LOG(
        DD_WHAT_USERID, "Cannot switch to user %s (uid: %u).", username, uid);
  }
  if (setgid(gid) != 0) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_USERID,
                           "Cannot switch to user's %s group (gid: %u)",
                           username, gid);
  }
  if (setegid(gid) != 0) {
    DDRES_RETURN_ERROR_LOG(
        DD_WHAT_USERID,
        "Cannot effectively switch to user's %s group (gid: %u)", username,
        gid);
  }
  if (setuid(uid) != 0) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_USERID, "Cannot switch to user %s (uid: %u)",
                           username, uid);
  }
  if (seteuid(uid) != 0) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_USERID,
                           "Cannot effectively switch to user %s (uid: %u)",
                           username, uid);
  }

  return {};
}
