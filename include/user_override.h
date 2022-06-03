// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres.h"
#include <sys/types.h>

typedef struct UIDInfo {
  bool override;
  uid_t previous_user;
} UIDInfo;

DDRes user_override_if_root(UIDInfo *override_info);

DDRes user_override(uid_t uid, UIDInfo *override_info);

DDRes revert_override(UIDInfo *override_info);
