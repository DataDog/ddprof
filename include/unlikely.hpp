// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#if defined(__has_builtin)
#  if __has_builtin(__builtin_expect)
#    define likely(x) __builtin_expect(!!(x), 1)
#    define unlikely(x) __builtin_expect(!!(x), 0)
#  else
#    define likely(x) (x)
#    define unlikely(x) (x)
#  endif
#else
#  define likely(x) (x)
#  define unlikely(x) (x)
#endif
