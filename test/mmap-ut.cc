// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

extern "C" {
#include "perf.h"
}

#include "syscalls.hpp"
#include "perf.hpp"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <string>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

// Simple test to see if mlock fails for large sizes
TEST(MMapTest, Mlock32KB) {
  const int alloc_size = 32 * 1024; // 32 KB
  char *memory = (char *)calloc(alloc_size, sizeof(char));
  ASSERT_TRUE(memory);
  int ret = mlock(memory, alloc_size);
  EXPECT_TRUE(ret == 0);
  munlock(memory, alloc_size);
  free(memory);
}

TEST(MMapTest, PerfOpen) {
  int pid = getpid();
  std::cerr << "Pid number" << pid << std::endl;

  long page_size = sysconf(_SC_PAGESIZE);
  std::cerr << "Page size is :" << page_size << std::endl;

  for (int i = 0; i < DDPROF_PWE_LENGTH; ++i) {
    std::cerr << "#######################################" << std::endl;
    std::cerr << "-->" << i << " " << ewatcher_from_idx(i)->desc << std::endl;

    const PerfWatcher *watcher = ewatcher_from_idx(i);
    std::vector<perf_event_attr> perf_event_data =
      ddprof::all_perf_configs_from_watcher(watcher, true);
    // test with the least restrictive conf
    int perf_fd = perf_event_open(&perf_event_data.back(), pid, 0, -1, PERF_FLAG_FD_CLOEXEC);

    // Pure-userspace software events should all pass.  Anything else should hit
    // this filter
    if (watcher->type != PERF_TYPE_SOFTWARE ||
        watcher->options.is_kernel == kPerfWatcher_Required) {
      continue;
    }
    EXPECT_TRUE(perf_fd != -1);
    if (perf_fd == -1) {
      std::cerr << "ERROR ---> :" << errno << "=" << strerror(errno)
                << std::endl;
      continue;
    }
    // default perfown is 4k * (64 + 1)
    size_t mmap_size = 0;
    void *region = NULL;
    int buf_size_shift = 10;
    while (region == nullptr && --buf_size_shift > 0) {
      mmap_size = perf_mmap_size(buf_size_shift);
      std::cerr << "mmap size attempt --> " << mmap_size << "("
                << buf_size_shift << ")";

      region = perfown_sz(perf_fd, mmap_size, false);

      if (!region) {
        std::cerr << " = FAILURE !!!" << std::endl;
        continue;
      }
      std::cerr << " = SUCCESS !!!" << std::endl;
      break;
    }
    if (region) {
      std::cerr << "FULL SUCCESS (size=" << mmap_size << ")" << std::endl;
    }
    ASSERT_TRUE(region);
    ASSERT_EQ(perfdisown(region, mmap_size, false), 0);
    close(perf_fd);
  }
}

TEST(MMapTest, Mirroring) {
  int buf_size_shift = 0;
  size_t mmap_size = perf_mmap_size(buf_size_shift);

  int fd = ddprof::memfd_create("foo", 0);
  ASSERT_NE(fd, -1);
  ASSERT_EQ(ftruncate(fd, mmap_size), 0);

  std::byte *region = static_cast<std::byte *>(perfown_sz(fd, mmap_size, true));
  ASSERT_TRUE(region);

  size_t usable_size = mmap_size - get_page_size();
  std::byte *start = region + get_page_size();
  std::byte *end = start + usable_size;

  *start = static_cast<std::byte>(0xff);
  EXPECT_EQ(*region, std::byte{0});
  EXPECT_EQ(*end, std::byte{0xff});

  close(fd);
  ASSERT_EQ(perfdisown(region, mmap_size, true), 0);
}
