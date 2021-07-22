#include "perf.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "logger.h"

#define rmb() __asm__ volatile("lfence" ::: "memory")

struct perf_event_attr g_dd_native_attr = {
    .size = sizeof(struct perf_event_attr),
    .sample_type = DEFAULT_SAMPLE_TYPE,
    .precise_ip = 2,
    .disabled = 1,
    .inherit = 1,
    .inherit_stat = 0,
    .mmap = 0, // keep track of executable mappings
    .task = 0, // Follow fork/stop events
    .comm = 0, // Follow exec()
    .enable_on_exec = 1,
    .sample_stack_user = PERF_SAMPLE_STACK_SIZE, // Is this an insane default?
    .sample_regs_user = PERF_REGS_MASK,
    .exclude_kernel = 1,
    .exclude_hv = 1,
};

int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu, int gfd,
                    unsigned long flags) {
  return syscall(__NR_perf_event_open, attr, pid, cpu, gfd, flags);
}

int perfopen(pid_t pid, PerfOption *opt, int cpu, bool extras) {
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

void *perfown(int fd) {
  void *region;

  // Map in the region representing the ring buffer
  // TODO what to do about hugepages?
  region = mmap(NULL, PAGE_SIZE + PSAMPLE_SIZE, PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, 0);
  if (MAP_FAILED == region || !region)
    return NULL;

  fcntl(fd, F_SETFL, O_RDWR | O_NONBLOCK);

  return region;
}

void rb_init(RingBuffer *rb, struct perf_event_mmap_page *page) {
  rb->start = (const char *)page + PAGE_SIZE;
}

uint64_t rb_next(RingBuffer *rb) {
  rb->offset = (rb->offset + sizeof(uint64_t)) & (PSAMPLE_SIZE - 1);
  return *(uint64_t *)(rb->start + rb->offset);
}

struct perf_event_header *rb_seek(RingBuffer *rb, uint64_t offset) {
  rb->offset = (unsigned long)offset & (PSAMPLE_SIZE - 1);
  return (struct perf_event_header *)(rb->start + rb->offset);
}

void main_loop(PEvent *pes, int pe_len, perfopen_attr *attr, void *arg) {
  struct pollfd pfd[100];
  assert(attr->msg_fun);

  if (pe_len > 100)
    pe_len = 100;

  // Setup poll() to watch perf_event file descriptors
  for (int i = 0; i < pe_len; i++) {
    // NOTE: if fd is negative, it will be ignored
    pfd[i].fd = pes[i].fd;
    pfd[i].events = POLLIN | POLLERR | POLLHUP;
  }
  while (1) {
    int n = poll(pfd, pe_len, PSAMPLE_DEFAULT_WAKEUP);

    // If there was an issue, return and let the caller check errno
    if (-1 == n && errno == EINTR)
      continue;
    else if (-1 == n)
      return;

    // If no file descriptors, call timed out
    if (0 == n && attr->timeout_fun) {
      attr->timeout_fun(arg);
      continue;
    }

    for (int i = 0; i < pe_len; i++) {
      if (!pfd[i].revents)
        continue;
      if (pfd[i].revents & POLLHUP)
        return;

      // Drain the ringbuffer and dispatch to callback, as needed
      // The head and tail are taken literally (without wraparound), since they
      // don't wrap in the underlying object.  Instead, the rb_* interfaces
      // wrap when accessing.
      uint64_t head = pes[i].region->data_head;
      rmb();
      uint64_t tail = pes[i].region->data_tail;
      RingBuffer *rb = &(RingBuffer){0};
      rb_init(rb, pes[i].region);

      while (head > tail) {
        struct perf_event_header *hdr = rb_seek(rb, tail);
        if ((void *)pes[i].region + PAGE_SIZE + PSAMPLE_SIZE <
            (void *)hdr + hdr->size) {
          // LG_WRN("[UNWIND] OUT OF BOUNDS");
        } else {
          attr->msg_fun(hdr, pes[i].pos, arg);
        }
        tail += hdr->size;
      }

      // We tell the kernel how much we read.  This *should* be the same as
      // the current tail, but in the case of an error head will be a safe
      // restart position.
      pes[i].region->data_tail = head;

      if (head != tail)
        LG_NTC("Head/tail buffer mismatch");
    }
  }
}
