// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#define DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION() __asm__ __volatile__("")
#define DDPROF_NOINLINE __attribute__((noinline))
#define DDPROF_NO_SANITIZER_ADDRESS __attribute__((no_sanitize("address")))

#if defined(__clang__)
#  define DDPROF_NOIPO __attribute__((noinline))
#else
#  define DDPROF_NOIPO __attribute__((noipa))
#endif
