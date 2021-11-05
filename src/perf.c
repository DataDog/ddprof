#include "perf.h"

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

#include "logger.h"
#include "user_override.h"

#define DEFAULT_PAGE_SIZE 4096 // Concerned about hugepages?

#define DEFAULT_BUFF_SIZE_SHIFT 5

static long s_page_size = 0;

struct perf_event_attr g_dd_native_attr = {
    .size = sizeof(struct perf_event_attr),
    .sample_type = DEFAULT_SAMPLE_TYPE,
    .precise_ip = 2,
    .disabled = 1,
    .inherit = 1,
    .inherit_stat = 0,
    .mmap = 0,      // keep track of executable mappings
    .mmap_data = 0, // keep track of other mappings
    .sample_id_all = 1,
    .task = 0, // Follow fork/stop events
    .comm = 0, // Follow exec()
    .enable_on_exec = 1,
    .sample_stack_user = PERF_SAMPLE_STACK_SIZE, // Is this an insane default?
    .sample_regs_user = PERF_REGS_MASK,
    .exclude_kernel = 1,
    .exclude_hv = 1,
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

int perfopen(pid_t pid, const PerfOption *opt, int cpu, bool extras) {
  struct perf_event_attr attr = g_dd_native_attr;
  attr.type = opt->type;
  attr.config = opt->config;
  attr.sample_period = opt->sample_period; // Equivalently, freq
  attr.exclude_kernel = !(opt->include_kernel);
  attr.freq = opt->freq;

  // Breakpoint
  if (opt->type & PERF_TYPE_BREAKPOINT) {
    attr.config = 0; // as per perf_event_open() manpage
    attr.bp_type = opt->bp_type;
  }

  // Extras
  if (extras) {
    attr.mmap = 1;
    attr.mmap_data = 1;
    attr.task = 1;
    attr.comm = 1;
  }

  int fd = perf_event_open(&attr, pid, cpu, -1, PERF_FLAG_FD_CLOEXEC);
  if (-1 == fd && EACCES == errno) {
    return -1;
  } else if (-1 == fd) {
    return -1;
  }

  return fd;
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
  void *region;

  // Map in the region representing the ring buffer
  // TODO what to do about hugepages?
  region =
      mmap(NULL, size_of_buffer, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  if (MAP_FAILED == region || !region)
    return NULL;

  if (fcntl(fd, F_SETFL, O_RDWR | O_NONBLOCK) == -1) {
    LG_WRN("Unable to run fcntl on %d", fd);
  }

  return region;
}

// returns region, size is updated with the mmaped size
// On failure, returns NULL
void *perfown(int fd, size_t *size) {
  *size = perf_mmap_size(DEFAULT_BUFF_SIZE_SHIFT);
  return perfown_sz(fd, *size);
}

int perfdisown(void *region, size_t size) { return munmap(region, size); }
