#ifndef _H_perf
#  define _H_perf

#  include <ctype.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <linux/hw_breakpoint.h>
#  include <linux/perf_event.h>
#  include <poll.h>
#  include <signal.h>
#  include <stdint.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#  include <sys/ioctl.h>
#  include <sys/mman.h>
#  include <sys/socket.h>
#  include <sys/syscall.h>
#  include <sys/types.h>
#  include <unistd.h>

#  define rmb() __asm__ volatile("lfence" ::: "memory")

#  define PAGE_SIZE 4096              // Concerned about hugepages?
#  define PSAMPLE_SIZE 64 * PAGE_SIZE // TODO check for high-volume
#  define PSAMPLE_DEFAULT_WAKEUP 1000 // sample frequency check
#  define PERF_SAMPLE_STACK_SIZE (4096 * 15)
#  define PERF_SAMPLE_STACK_REGS 3
#  define MAX_INSN 16

typedef struct PEvent {
  int pos; // Index into the sample
  int fd;  // Underlying perf event FD
  struct perf_event_mmap_page *region;
} PEvent;

// TODO, this comes from BP, SP, and IP
// see arch/x86/include/uapi/asm/perf_regs.h in the linux sources
// We're going to hardcode everything for now...
#  define PERF_REGS_MASK ((1 << 6) | (1 << 7) | (1 << 8))

typedef struct read_format {
  uint64_t value;        // The value of the event
  uint64_t time_enabled; // if PERF_FORMAT_TOTAL_TIME_ENABLED
  uint64_t time_running; // if PERF_FORMAT_TOTAL_TIME_RUNNING
  //  uint64_t id;            // if PERF_FORMAT_ID
} read_format;

typedef struct sample_id {
  uint32_t pid, tid; /* if PERF_SAMPLE_TID set */
  uint64_t time;     /* if PERF_SAMPLE_TIME set */
  //  uint64_t id;         /* if PERF_SAMPLE_ID set */
  //  uint64_t stream_id;  /* if PERF_SAMPLE_STREAM_ID set  */
  //  uint32_t cpu, res;   /* if PERF_SAMPLE_CPU set */
  //  uint64_t id;         /* if PERF_SAMPLE_IDENTIFIER set */
} sample_id;

typedef struct perf_event_exit {
  struct perf_event_header header;
  uint32_t pid, ppid;
  uint32_t tid, ptid;
  uint64_t time;
  struct sample_id sample_id;
} perf_event_exit;

typedef struct perf_event_fork {
  struct perf_event_header header;
  uint32_t pid, ppid;
  uint32_t tid, ptid;
  uint64_t time;
  struct sample_id sample_id;
} perf_event_fork;

typedef struct perf_event_mmap {
  struct perf_event_header header;
  uint32_t pid, tid;
  uint64_t addr;
  uint64_t len;
  uint64_t pgoff;
  char filename[];
} perf_event_mmap;

typedef struct perf_event_comm {
  struct perf_event_header header;
  uint32_t pid;
  uint32_t tid;
  char comm[];
} perf_event_comm;

typedef struct perf_event_lost {
  struct perf_event_header header;
  uint64_t id;
  uint64_t lost;
  struct sample_id sample_id;
} perf_event_lost;

// clang-format off
typedef struct perf_event_sample {
  struct perf_event_header header;
  //  uint64_t    sample_id;                  // if PERF_SAMPLE_IDENTIFIER
  uint64_t ip;       // if PERF_SAMPLE_IP
  uint32_t pid, tid; // if PERF_SAMPLE_TID
  uint64_t time;     // if PERF_SAMPLE_TIME
//  uint64_t    addr;                       // if PERF_SAMPLE_ADDR
//  uint64_t    id;                         // if PERF_SAMPLE_ID
  //  uint64_t    stream_id;                  // if PERF_SAMPLE_STREAM_ID
  //  uint32_t    cpu, res;                   // if PERF_SAMPLE_CPU
  uint64_t period; // if PERF_SAMPLE_PERIOD
//  struct read_format v;              // if PERF_SAMPLE_READ
//  uint64_t    nr;                         // if PERF_SAMPLE_CALLCHAIN
//  uint64_t    ips[];                      // if PERF_SAMPLE_CALLCHAIN
//  uint32_t    size;                       // if PERF_SAMPLE_RAW
//  char  data[size];                  // if PERF_SAMPLE_RAW
  //  uint64_t    bnr;                        // if PERF_SAMPLE_BRANCH_STACK
  //  struct perf_branch_entry lbr[bnr]; // if PERF_SAMPLE_BRANCH_STACK
  uint64_t abi;                           // if PERF_SAMPLE_REGS_USER
  uint64_t regs[PERF_SAMPLE_STACK_REGS];  // if PERF_SAMPLE_REGS_USER
  uint64_t size;                          // if PERF_SAMPLE_STACK_USER
  char data[PERF_SAMPLE_STACK_SIZE]; // if PERF_SAMPLE_STACK_USER
  uint64_t dyn_size;                      // if PERF_SAMPLE_STACK_USER
  //  uint64_t    weight;                     // if PERF_SAMPLE_WEIGHT
  //  uint64_t    data_src;                   // if PERF_SAMPLE_DATA_SRC
  //  uint64_t    transaction;                // if PERF_SAMPLE_TRANSACTION
  //  uint64_t    abi;                        // if PERF_SAMPLE_REGS_INTR
  //  uint64_t    regs[weight(mask)];         // if PERF_SAMPLE_REGS_INTR
} perf_event_sample;
// clang-format on

typedef struct perf_samplestacku {
  uint64_t size;
  char data[];
  // uint64_t    dyn_size;   // Don't forget!
} perf_samplestacku;

<<<<<<< HEAD
typedef struct BPDef {
  uint64_t bp_addr;
  uint64_t bp_len;
} BPDef;

typedef struct PerfOption {
  char *desc;
  char *key;
  int type;
  union {
    unsigned long config;
    BPDef *bp;
  };
  union {
    uint64_t sample_period;
    uint64_t sample_frequency;
  };
  char *label;
  char *unit;
  int mode;
  bool include_kernel;
  bool freq;
  char bp_type;
} PerfOption;

// Architecturally, it's probably simpler to split this out into callbacks for
// sample, lost, mmap, comm, etc type events; but for now we'll absorb all
// of that into msg_fun
typedef struct perfopen_attr {
  void (*msg_fun)(struct perf_event_header *, int, void *);
  void (*timeout_fun)(void *);
} perfopen_attr;

struct perf_event_attr g_dd_native_attr = {
    .size = sizeof(struct perf_event_attr),
    .sample_type = PERF_SAMPLE_STACK_USER | PERF_SAMPLE_REGS_USER |
        PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME |
        PERF_SAMPLE_PERIOD,
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

// Used by rb_init() and friends
typedef struct RingBuffer {
  const char *start;
  unsigned long offset;
} RingBuffer;

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
    if (-1 == n &&errno = EINTR)
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

      uint64_t head = pes[i].region->data_head & (PSAMPLE_SIZE - 1);
      uint64_t tail = pes[i].region->data_tail & (PSAMPLE_SIZE - 1);

      rmb();

      RingBuffer *rb = &(RingBuffer){0};
      rb_init(rb, pes[i].region);

      int elems = 0;
      while (head > tail) {
        elems++;
        struct perf_event_header *hdr = rb_seek(rb, tail);

        attr->msg_fun(hdr, pes[i].pos, arg);

        tail += hdr->size;
        tail = tail & (PSAMPLE_SIZE - 1);
      }
      pes[i].region->data_tail = head;
    }
  }
}

#  endif // _H_perf
