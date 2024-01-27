// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

typedef signed char __s8;
typedef unsigned char __u8;
typedef short int __s16;
typedef short unsigned int __u16;
typedef int __s32;
typedef unsigned int __u32;
typedef long long int __s64;
typedef long long unsigned int __u64;
typedef __s8 s8;
typedef __u8 u8;
typedef __s16 s16;
typedef __u16 u16;
typedef __s32 s32;
typedef __u32 u32;

typedef __s64 s64;

typedef __u64 u64;

#include "ddres_def.hpp"
#include "bpf/sample_processor.h"

namespace ddprof {

struct UnwindState;

DDRes unwind_init_dwfl(UnwindState *us);

DDRes unwind_dwfl(UnwindState *us);

DDRes unwind_symbolize_only(UnwindState *us, stacktrace_event &event);

} // namespace ddprof
