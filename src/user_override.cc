// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include "user_override.hpp"

#include "logger.hpp"

#include <cstring>
#include <cerrno>
#include <grp.h>
#include <pwd.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
constexpr const char *const s_user_nobody = "nobody";
constexpr uid_t s_root_user = 0;

struct DumpableRestorer {
public:
  DumpableRestorer() { _dumpable = prctl(PR_GET_DUMPABLE); }
  ~DumpableRestorer() { prctl(PR_SET_DUMPABLE, _dumpable); }

private:
  int _dumpable;
};

} // namespace

bool is_root() { return getuid() == s_root_user; }

DDRes user_override_to_nobody_if_root(UIDInfo *old_uids) {
  if (!is_root()) {
    if (old_uids) {
      *old_uids = {};
    }
    return {};
  }
  return user_override(s_user_nobody, old_uids);
}

DDRes user_override(const char *user, UIDInfo *old_uids) {
  struct passwd *pw = getpwnam(user);
  if (!pw) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_USERID, "Unable to find user %s", user);
  }

  return user_override(pw->pw_uid, pw->pw_gid, old_uids);
}

DDRes user_override(uid_t uid, gid_t gid, UIDInfo *old_uids) {
  if (old_uids) {
    old_uids->gid = getgid();
    old_uids->uid = getuid();
  }

  // Early exit if nothing to do
  if (uid == static_cast<uid_t>(-1) && gid == static_cast<uid_t>(-1)) {
    return {};
  }

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
  DumpableRestorer dumpable_restorer;
  if (gid != static_cast<uid_t>(-1)) {
    DDRES_CHECK_INT(setresgid(gid, gid, -1), DD_WHAT_USERID,
                    "Unable to set gid %d (%s)", gid, std::strerror(errno));
  }
  if (uid != static_cast<uid_t>(-1)) {
    DDRES_CHECK_INT(setresuid(uid, uid, -1), DD_WHAT_USERID,
                    "Unable to set uid %d (%s)", uid, std::strerror(errno));
  }

  return {};
}

DDRes become_user(const char *username) {
  // Inspired from
  // https://github.com/netdata/netdata/blob/71cb1ad68707718671ef57c901dfa2041f15bbe6/daemon/daemon.c#L77

  DumpableRestorer dumpable_restorer;
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
