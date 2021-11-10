// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stddef.h>
#include <string.h>

// minor: Declaring this as a single header in libddprof would avoid duplication

typedef struct string_view {
  const char *ptr;
  size_t len;
} string_view;

static inline string_view string_view_create(const char *ptr, size_t len) {
  return (string_view){.ptr = (ptr), len};
}

static inline string_view string_view_create_strlen(const char *ptr) {
  return string_view_create(ptr, strlen(ptr));
}

#define STRING_VIEW_LITERAL(literal)                                           \
  (string_view) { .ptr = literal, .len = sizeof(literal) - 1 }
