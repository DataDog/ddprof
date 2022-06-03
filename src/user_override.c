// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include "user_override.h"

#include "logger.h"
#include "unistd.h"

#include <errno.h>
#include <pwd.h>
#include <string.h>
#include <unistd.h>

static char const *const s_user_nobody = "nobody";
static const uid_t s_root_user = 0;

void init_uidinfo(UIDInfo *override_info) {
  override_info->override = false;
  override_info->previous_user = getuid();
}

DDRes user_override_if_root(UIDInfo *override_info) {
  init_uidinfo(override_info);

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
  DDRES_CHECK_FWD(user_override(nobodyuid, override_info));
  override_info->override = true;
  return ddres_init();
}


DDRes user_override(uid_t uid, UIDInfo *override_info) {
  uid_t current_user = getuid();
  if (getuid() == uid) {
    // Already this user
    LG_DBG("Requesting a change for the same user");
    return ddres_notice(DD_WHAT_USERID);
  }
  DDRES_CHECK_INT(setresuid(uid, uid, -1), DD_WHAT_USERID,
                  "Unable to set user %u (%s)", uid, strerror(errno));
  override_info->override = true;
  override_info->previous_user = current_user;
  return ddres_init();
}

DDRes revert_override(UIDInfo *override_info) {
  if (!(override_info->override)) {
    // nothing to do we did not override previously
    return ddres_init();
  }
  uid_t uid_old = override_info->previous_user;
  DDRES_CHECK_INT(setresuid(uid_old, uid_old, -1), DD_WHAT_USERID,
                  "Unable to set user %s (%s)", s_user_nobody, strerror(errno));
  init_uidinfo(override_info);
  return ddres_init();
}
