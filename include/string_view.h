#pragma once

#include <stddef.h>

// minor: Declaring this as a single header in libddprof would avoid duplication

typedef struct string_view {
  const char *ptr;
  size_t len;
} string_view;

static inline string_view string_view_create(const char *ptr, size_t len) {
  string_view temp = {.ptr = ptr, .len = len};
  return temp;
}
