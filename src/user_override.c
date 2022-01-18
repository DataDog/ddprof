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

void init_uidinfo(UIDInfo *user_override) {
  user_override->override = false;
  user_override->previous_user = getuid();
}

DDRes user_override(UIDInfo *user_override) {
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
  DDRES_CHECK_INT(setresuid(nobodyuid, nobodyuid, -1), DD_WHAT_USERID,
                  "Unable to set user %s (%s)", s_user_nobody, strerror(errno));
  user_override->override = true;

  return ddres_init();
}

DDRes revert_override(UIDInfo *user_override) {
  if (!(user_override->override)) {
    // nothing to do we did not override previously
    return ddres_init();
  }
  uid_t uid_old = user_override->previous_user;
  DDRES_CHECK_INT(setresuid(uid_old, uid_old, -1), DD_WHAT_USERID,
                  "Unable to set user %s (%s)", s_user_nobody, strerror(errno));
  init_uidinfo(user_override);
  return ddres_init();
}
