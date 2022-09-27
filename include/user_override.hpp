// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres.hpp"
#include <sys/types.h>

struct UIDInfo {
  uid_t uid = -1;
  gid_t gid = -1;
};

// Change real and effective user and group ids
DDRes user_override_to_nobody_if_root(UIDInfo *old_uids = nullptr);
DDRes user_override(const char *user, UIDInfo *old_uids = nullptr);
DDRes user_override(uid_t uid, gid_t gid, UIDInfo *old_uids = nullptr);

// Irreversibly switch to user `user`
DDRes become_user(const char *user);
