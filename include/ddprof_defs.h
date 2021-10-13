#pragma once

#include <stdint.h>

/// Maximum number of different watcher types
#define MAX_TYPE_WATCHER 10

// Maximum depth for a single stack
#define DD_MAX_STACK_DEPTH 1024

// unique identifier to serve as a key for Dso
typedef uint32_t DsoUID_t;

typedef int32_t SymbolIdx_t;
typedef int32_t MapInfoIdx_t;
// Absolute address (needs to be adjusted with the start address of binary)
typedef uint64_t ElfAddress_t;
// Address within a binary : adjust doing (abs_addr - start_bin) + offset
typedef ElfAddress_t Offset_t;
