#pragma once

#include "ddres.h"
#include <sys/types.h>

typedef struct UIDInfo {
  bool override;
  uid_t previous_user;
} UIDInfo;

DDRes user_override(UIDInfo *user_override);

DDRes revert_override(UIDInfo *user_override);
