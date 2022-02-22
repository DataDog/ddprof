// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#ifdef __x86_64
#  include <x86intrin.h>
#elif __aarch64__
#  define __rdtsc() 0
#endif
