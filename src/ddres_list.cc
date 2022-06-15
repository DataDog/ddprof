// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddres.h"

static const char *s_common_error_messages[] = {
    "Common start range", COMMOM_ERROR_TABLE(EXPAND_ERROR_MESSAGE)};

static const char *s_native_error_messages[] = {
    "native start range", NATIVE_ERROR_TABLE(EXPAND_ERROR_MESSAGE)};

const char *ddres_error_message(int16_t what) {
  if (what < DD_WHAT_MIN_ERNO) {
    // should we handle errno here ?
    return "Check errno";
    // Range of [DD_WHAT_MIN_ERNO; COMMON_ERROR_SIZE[ range
  } else if (what < COMMON_ERROR_SIZE) {
    return s_common_error_messages[what - DD_COMMON_START_RANGE];
  } else if (what >= DD_NATIVE_START_RANGE && what < NATIVE_ERROR_SIZE) {
    return s_native_error_messages[what - DD_NATIVE_START_RANGE];
  }
  return "Unknown error. Please update " __FILE__;
}
