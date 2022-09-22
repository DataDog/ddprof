// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf.hpp"

#include "defer.hpp"
#include "logger.hpp"
#include "perf.hpp"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#define DEFAULT_PAGE_SIZE 4096 // Concerned about hugepages?

static long s_page_size = 0;

struct perf_event_attr g_dd_native_attr = {
    .size = sizeof(struct perf_event_attr),
    .disabled = 1,
    .inherit = 1,
    .exclude_kernel = 1,
    .exclude_hv = 1,
    .mmap = 0, // keep track of executable mappings
    .comm = 0, // Follow exec()
    .inherit_stat = 0,
    .enable_on_exec = 1,
    .task = 0, // Follow fork/stop events
    .precise_ip = 2,
    .mmap_data = 0, // keep track of other mappings
    .sample_id_all = 1,
    .sample_regs_user = PERF_REGS_MASK,
};

long get_page_size(void) {
  if (!s_page_size) {
    s_page_size = sysconf(_SC_PAGESIZE);
    // log if we have an unusual page size
    if (s_page_size != DEFAULT_PAGE_SIZE)
      LG_WRN("Page size is %ld", s_page_size);
  }
  return s_page_size;
}

int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu, int gfd,
                    unsigned long flags) {
  return syscall(__NR_perf_event_open, attr, pid, cpu, gfd, flags);
}

const char *perf_type_str(int type_id) {
  switch (type_id) {
  case PERF_TYPE_HARDWARE:
    return "HARDWARE";
  case PERF_TYPE_SOFTWARE:
    return "SOFTWARE";
  case PERF_TYPE_TRACEPOINT:
    return "TRACEPOINT";
  case PERF_TYPE_HW_CACHE:
    return "HW_CACHE";
  case PERF_TYPE_RAW:
    return "RAW";
  case PERF_TYPE_BREAKPOINT:
    return "BREAKPOINT";
  default:
    return "UNKNOWN";
  }
  return "UNKNOWN";
}

perf_event_attr perf_config_from_watcher(const PerfWatcher *watcher,
                                         bool extras) {
  struct perf_event_attr attr = g_dd_native_attr;
  attr.type = watcher->type;
  attr.config = watcher->config;
  attr.sample_period = watcher->sample_period; // Equivalently, freq
  attr.freq = watcher->options.is_freq;
  attr.sample_type = watcher->sample_type;
  attr.sample_stack_user = watcher->sample_stack_size;

  // If is_kernel is requested false --> exclude_kernel == true
  attr.exclude_kernel = (watcher->options.is_kernel == kPerfWatcher_Off);

  // Extras (metadata for tracking process state)
  if (extras) {
    attr.mmap = 1;
    attr.mmap2 = 1;
    attr.task = 1;
    attr.comm = 1;
  }
  return attr;
}

size_t perf_mmap_size(int buf_size_shift) {
  // size of buffers are constrained to a power of 2 + 1
  return ((1U << buf_size_shift) + 1) * get_page_size();
}

size_t get_mask_from_size(size_t size) {
  // assumption is that we used a (power of 2) + 1 (refer to perf_mmap_size)
  return (size - get_page_size() - 1);
}

void *perfown_sz(int fd, size_t size_of_buffer) {
  // Map in the region representing the ring buffer, map the buffer twice
  // (minus metadata size) to avoid handling boundaries.
  size_t total_length = 2 * size_of_buffer - get_page_size();
  // Reserve twice the size of the buffer
  void *region =
      mmap(NULL, total_length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (MAP_FAILED == region || !region)
    return NULL;

  auto defer_munmap = make_defer([&]() { perfdisown(region, size_of_buffer); });

  std::byte *ptr = static_cast<std::byte *>(region);

  // Each mapping of fd must have a size of 2^n+1 pages
  // That's why starts by mapping buffer on the second half of reserved
  // space and ensure that metadata page overlaps on the first part
  if (mmap(ptr + size_of_buffer - get_page_size(), size_of_buffer,
           PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED)
    return NULL;

  // Map buffer a second time on the first half of reserved space
  // It will overlap the metadata page of the previous mapping.
  if (mmap(ptr, size_of_buffer, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
           fd, 0) == MAP_FAILED)
    return NULL;

  defer_munmap.release();

  if (fcntl(fd, F_SETFL, O_RDWR | O_NONBLOCK) == -1) {
    LG_WRN("Unable to run fcntl on %d", fd);
  }

  return region;
}

int perfdisown(void *region, size_t size) {
  std::byte *ptr = static_cast<std::byte *>(region);

  return (munmap(ptr + size - get_page_size(), size) == 0) &&
          (munmap(ptr, size) == 0) &&
          (munmap(ptr, 2 * size - get_page_size()) == 0)
      ? 0
      : -1;
}

namespace ddprof {
// return attr sorted by priority
std::vector<perf_event_attr>
all_perf_configs_from_watcher(const PerfWatcher *watcher, bool extras) {
  std::vector<perf_event_attr> ret_attr;
  ret_attr.push_back(perf_config_from_watcher(watcher, extras));
  if (watcher->options.is_kernel == kPerfWatcher_Try) {
    // duplicate the config, while excluding kernel
    ret_attr.push_back(ret_attr.back());
    ret_attr.back().exclude_kernel = true;
  }
  return ret_attr;
}
} // namespace ddprof
