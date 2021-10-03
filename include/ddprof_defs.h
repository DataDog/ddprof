#pragma once

#include <stdint.h>

/// Maximum number of different watcher types
#define MAX_TYPE_WATCHER 10

// Maximum depth for a single stack
#define DD_MAX_STACK 1024

// unique identifier to serve as a key for Dso
typedef uint32_t DsoUID_t;

typedef int32_t IPInfoIdx_t;
typedef int32_t MapInfoIdx_t;
typedef uint64_t ElfAddress_t;
typedef ElfAddress_t Offset_t;
