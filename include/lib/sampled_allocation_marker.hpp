// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

namespace ddprof::marker {

#if defined(__aarch64__)

// ARM64: Top Byte Ignore (TBI) pointer tagging.
// Bit 60 is used to avoid conflict with MTE (Memory Tagging Extension)
// which uses bits 59:56.
inline constexpr uintptr_t kSampledBitMask = 1ULL << 60;

inline void *tag(void *ptr) {
  return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(ptr) |
                                  kSampledBitMask);
}

inline bool is_tagged(const void *ptr) {
  return (reinterpret_cast<uintptr_t>(ptr) & kSampledBitMask) != 0;
}

inline void *untag(void *ptr) {
  return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(ptr) &
                                  ~kSampledBitMask);
}

#elif defined(__x86_64__)

// AMD64: Hidden prefix before user pointer for non-mmap allocations.
// The prefix contains a magic sentinel and the offset from the user pointer
// back to the original allocation. On free, reading the prefix identifies
// sampled allocations without needing a hash table lookup.
inline constexpr uint64_t kMagic = 0xDD9F0F5A3901E001ULL;
inline constexpr size_t kMinPrefixSize = 16;

struct Prefix {
  uint64_t magic;
  uint64_t offset; // bytes from original_ptr to user_ptr
};
static_assert(sizeof(Prefix) <= kMinPrefixSize);

// Returns the prefix size needed to maintain the given alignment.
// For standard malloc (alignment=16), returns 16.
// For aligned_alloc(256, ...), returns 256.
inline constexpr size_t prefix_size_for_alignment(size_t alignment) {
  return alignment >= kMinPrefixSize ? alignment : kMinPrefixSize;
}

// Write prefix and return user pointer. raw_ptr is from the real allocator.
// alignment is the user-requested alignment (default 16 for malloc).
inline void *write_prefix(void *raw_ptr, size_t alignment) {
  size_t psize = prefix_size_for_alignment(alignment);
  auto *user_ptr = static_cast<std::byte *>(raw_ptr) + psize;
  // Prefix is always immediately before the user pointer
  auto *p = reinterpret_cast<Prefix *>(user_ptr - sizeof(Prefix));
  p->magic = kMagic;
  p->offset = psize;
  return user_ptr;
}

// Check if ptr has a prefix. Returns {is_sampled, original_ptr}.
inline std::pair<bool, void *> read_prefix(void *ptr) {
  auto *p = reinterpret_cast<Prefix *>(static_cast<std::byte *>(ptr) -
                                       sizeof(Prefix));
  if (p->magic == kMagic) {
    return {true, static_cast<std::byte *>(ptr) - p->offset};
  }
  return {false, nullptr};
}

inline bool is_page_aligned(const void *ptr) {
  return (reinterpret_cast<uintptr_t>(ptr) & 0xFFF) == 0;
}

#endif

} // namespace ddprof::marker
