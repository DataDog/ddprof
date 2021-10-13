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

// All values coming off of perf_event_open() structs are 8-byte aligned, but
// evidently the interface doesn't pad variable-length members, so realignment
// is needed
#define DEFAULT_PAGE_SIZE 4096 // Concerned about hugepages?

#define DEFAULT_BUFF_SIZE_SHIFT 6
#define RETRY_BUFF_SIZE_SHIFT 3

static long s_page_size = 0;

struct perf_event_attr g_dd_native_attr = {
    .size = sizeof(struct perf_event_attr),
    .sample_type = DEFAULT_SAMPLE_TYPE,
    .precise_ip = 2,
    .disabled = 1,
    .inherit = 1,
    .inherit_stat = 0,
    .mmap = 0, // keep track of executable mappings
    .mmap_data = 0, // keep track of other mappings
    .sample_id_all = 0,
    .task = 0, // Follow fork/stop events
    .comm = 0, // Follow exec()
    .enable_on_exec = 1,
    .sample_stack_user = PERF_SAMPLE_STACK_SIZE, // Is this an insane default?
    .sample_regs_user = PERF_REGS_MASK,
    .exclude_kernel = 1,
    .exclude_hv = 1,
};

static long get_page_size(void) {
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

// returns region, size is updated with the attempted size
// On failure, returns NULL
void *perfown(int fd, size_t *size) {
  *size = perf_mmap_size(DEFAULT_BUFF_SIZE_SHIFT);
  void *reg = perfown_sz(fd, *size);
  if (reg)
    return reg;
  *size = perf_mmap_size(RETRY_BUFF_SIZE_SHIFT);
  return perfown_sz(fd, *size);
}

int perfdisown(void *region, size_t size) { return munmap(region, size); }

void rb_init(RingBuffer *rb, struct perf_event_mmap_page *page, size_t size) {
  rb->meta_size = get_page_size();
  rb->start = (const char *)page + rb->meta_size;
  rb->size = size;
  rb->mask = get_mask_from_size(size);
}

uint64_t rb_next(RingBuffer *rb) {
  rb->offset = (rb->offset + sizeof(uint64_t)) & (rb->mask);
  return *(uint64_t *)(rb->start + rb->offset);
}

struct perf_event_header *rb_seek(RingBuffer *rb, uint64_t offset) {
  rb->offset = (unsigned long)offset & (rb->mask);
  return (struct perf_event_header *)(rb->start + rb->offset);
}

// This union is an implementation trick to make splitting apart an 8-byte
// aligned block into two 4-byte blocks easier
typedef union flipper {
  uint64_t full;
  uint32_t half[2];
} flipper;

perf_event_sample *hdr2samp(struct perf_event_header *hdr) {
  static perf_event_sample sample = {0};
  memset(&sample, 0, sizeof(sample));

  uint64_t *buf = (uint64_t *)(hdr + 1);

  if (PERF_SAMPLE_IDENTIFIER & DEFAULT_SAMPLE_TYPE) {
    sample.sample_id = *buf++;
  }
  if (PERF_SAMPLE_IP & DEFAULT_SAMPLE_TYPE) {
    sample.ip = *buf++;
  }
  if (PERF_SAMPLE_TID & DEFAULT_SAMPLE_TYPE) {
    sample.pid = ((flipper *)buf)->half[0];
    sample.tid = ((flipper *)buf)->half[1];
    buf++;
  }
  if (PERF_SAMPLE_TIME & DEFAULT_SAMPLE_TYPE) {
    sample.time = *buf++;
  }
  if (PERF_SAMPLE_ADDR & DEFAULT_SAMPLE_TYPE) {
    sample.addr = *buf++;
  }
  if (PERF_SAMPLE_ID & DEFAULT_SAMPLE_TYPE) {
    sample.id = *buf++;
  }
  if (PERF_SAMPLE_STREAM_ID & DEFAULT_SAMPLE_TYPE) {
    sample.stream_id = *buf++;
  }
  if (PERF_SAMPLE_CPU & DEFAULT_SAMPLE_TYPE) {
    sample.cpu = ((flipper *)buf)->half[0];
    sample.res = ((flipper *)buf)->half[1];
    buf++;
  }
  if (PERF_SAMPLE_PERIOD & DEFAULT_SAMPLE_TYPE) {
    sample.period = *buf++;
  }
  if (PERF_SAMPLE_READ & DEFAULT_SAMPLE_TYPE) {
    sample.v = (struct read_format *)buf++;
  }
  if (PERF_SAMPLE_CALLCHAIN & DEFAULT_SAMPLE_TYPE) {
    sample.nr = *buf++;
    sample.ips = buf;
    buf += sample.nr;
  }
  if (PERF_SAMPLE_RAW & DEFAULT_SAMPLE_TYPE) {}
  if (PERF_SAMPLE_BRANCH_STACK & DEFAULT_SAMPLE_TYPE) {}
  if (PERF_SAMPLE_REGS_USER & DEFAULT_SAMPLE_TYPE) {
    sample.abi = *buf++;
    sample.regs = buf;
    buf += PERF_REGS_COUNT;
  }
  if (PERF_SAMPLE_STACK_USER & DEFAULT_SAMPLE_TYPE) {
    uint64_t size_stack = *buf++;

    // Empirically, it seems that the size of the static stack is either 0 or
    // the amount requested in the call to `perf_event_open()`.  We don't check
    // for that, since there isn't much we'd be able to do anyway.
    if (size_stack == 0) {
      sample.size_stack = 0;
      sample.data_stack = NULL;
    } else {
      uint64_t dynsz_stack = 0;
      sample.data_stack = (char *)buf;
      buf = (uint64_t *)(sample.data_stack + size_stack);

      // If the size was specified, we also have a dyn_size
      dynsz_stack = *buf++;

      // If dynsize is too big, that's an error, but right now just roll it back
      // This doesn't affect deserialization.
      sample.size_stack = size_stack <= dynsz_stack ? size_stack : dynsz_stack;
    }
  }
  if (PERF_SAMPLE_WEIGHT & DEFAULT_SAMPLE_TYPE) {}
  if (PERF_SAMPLE_DATA_SRC & DEFAULT_SAMPLE_TYPE) {}
  if (PERF_SAMPLE_TRANSACTION & DEFAULT_SAMPLE_TYPE) {}
  if (PERF_SAMPLE_REGS_INTR & DEFAULT_SAMPLE_TYPE) {}

  // Ensure buf can be used in a semantically correct way without worrying
  // whether we've implemented the next consumer.  This is to keep static
  // analysis and checkers happy.
  (void)buf;

  return &sample;
}
