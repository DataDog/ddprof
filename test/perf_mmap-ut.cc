// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf.hpp"
#include "syscalls.hpp"
#include "unique_fd.hpp"

#include <cerrno>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {
unsigned fixed_mapping_count = 0;
bool reject_second_mapping = false;
} // namespace

extern "C" void *__real_mmap(void *addr, size_t length, int prot, int flags,
                             int fd, off_t offset);

extern "C" __attribute__((visibility("default"))) void *
__wrap_mmap(void *addr, size_t length, int prot, int flags, int fd,
            off_t offset) {
  if ((flags & MAP_FIXED) != 0) {
    ++fixed_mapping_count;
    if (reject_second_mapping && fixed_mapping_count == 2) {
      errno = ENOMEM;
      return MAP_FAILED;
    }
  }
  return __real_mmap(addr, length, prot, flags, fd, offset);
}

namespace ddprof {

TEST(PerfMmapTest, CacheSuccessfulFallbackOnlyForPerfBuffers) {
  size_t const size = perf_mmap_size(0);
  UniqueFd const fd{memfd_create("perf-mmap-test", 0)};
  ASSERT_TRUE(fd);
  ASSERT_EQ(ftruncate(fd.get(), size), 0);

  PerfMmapRegion mapping = perfown_sz(-1, size, true);
  ASSERT_FALSE(mapping.addr);

  fixed_mapping_count = 0;
  mapping = perfown_sz(fd.get(), size, true);
  ASSERT_TRUE(mapping.addr);
  EXPECT_TRUE(mapping.mirrored);
  EXPECT_EQ(fixed_mapping_count, 2);
  ASSERT_EQ(perfdisown(mapping.addr, size, mapping.mirrored), 0);

  fixed_mapping_count = 0;
  reject_second_mapping = true;
  mapping = perfown_sz(fd.get(), size, true);
  ASSERT_TRUE(mapping.addr);
  EXPECT_FALSE(mapping.mirrored);
  EXPECT_EQ(fixed_mapping_count, 2);
  ASSERT_EQ(perfdisown(mapping.addr, size, mapping.mirrored), 0);

  fixed_mapping_count = 0;
  reject_second_mapping = false;
  mapping = perfown_sz(fd.get(), size, true);
  ASSERT_TRUE(mapping.addr);
  EXPECT_FALSE(mapping.mirrored);
  EXPECT_EQ(fixed_mapping_count, 0);
  ASSERT_EQ(perfdisown(mapping.addr, size, mapping.mirrored), 0);

  fixed_mapping_count = 0;
  reject_second_mapping = true;
  mapping = perfown_sz(fd.get(), size, false);
  ASSERT_FALSE(mapping.addr);
  EXPECT_EQ(fixed_mapping_count, 2);

  fixed_mapping_count = 0;
  reject_second_mapping = false;
  mapping = perfown_sz(fd.get(), size, false);
  ASSERT_TRUE(mapping.addr);
  EXPECT_TRUE(mapping.mirrored);
  EXPECT_EQ(fixed_mapping_count, 2);
  auto *region = static_cast<std::byte *>(mapping.addr);
  region[get_page_size()] = std::byte{0x42};
  EXPECT_EQ(region[size], std::byte{0x42});
  ASSERT_EQ(perfdisown(mapping.addr, size, mapping.mirrored), 0);
}

} // namespace ddprof
