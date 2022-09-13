#pragma once

#ifdef __x86_64__
#include_next "cpuid.h"
#else
#define bit_XSAVE       (1 << 26)
#define bit_AVX         (1 << 28)

static __inline int
__get_cpuid (unsigned int __leaf,
             unsigned int *__eax, unsigned int *__ebx,
             unsigned int *__ecx, unsigned int *__edx)
{
    return 0;
}

static __inline int
__get_cpuid_count (unsigned int __leaf, unsigned int __subleaf,
                   unsigned int *__eax, unsigned int *__ebx,
                   unsigned int *__ecx, unsigned int *__edx)
{
    return 0;
}
#endif
