#include "ddres.h"

static const char *s_common_error_messages[] = {
    "Common start range", COMMOM_ERROR_TABLE(EXPAND_ERROR_MESSAGE)};

static const char *s_native_error_messages[] = {
    "native start range", NATIVE_ERROR_TABLE(EXPAND_ERROR_MESSAGE)};

const char *ddres_error_message(int16_t what) {
  if (what < DD_WHAT_MIN_ERNO) {
    // should we handle errno here ?
    return "Check errno";
  } else if (what >= DD_COMMON_START_RANGE && what < COMMON_ERROR_SIZE) {
    return s_common_error_messages[what - DD_COMMON_START_RANGE];
  } else if (what >= DD_NATIVE_START_RANGE && what < NATIVE_ERROR_SIZE) {
    return s_native_error_messages[what - DD_NATIVE_START_RANGE];
  }
  return "Unknown error. Please update " __FILE__;
}
