// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stddef.h>

#ifdef __cplusplus
template <typename T, size_t N> char (&ARRAY_SIZE_HELPER(T (&array)[N]))[N];
#  define ARRAY_SIZE(array) (sizeof(ARRAY_SIZE_HELPER(array)))
#else
#  define BUILD_BUG_ON_ZERO(e) (sizeof(struct { int : -!!(e)*1234; }))
#  define SAME_TYPE(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))
#  define MUST_BE_ARRAY(a) BUILD_BUG_ON_ZERO(SAME_TYPE((a), &(*a)))

#  define ARRAY_SIZE(a) ((sizeof(a) / sizeof(*a)) + MUST_BE_ARRAY(a))
#endif
