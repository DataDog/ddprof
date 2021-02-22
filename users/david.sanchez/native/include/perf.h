#ifndef _H_perf
#define _H_perf

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h> // Why do I suddenly need this?
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

/* TODO
    - Check support/permissions by reading /proc/sys/kernel/perf_event_paranoid
*/

#define rmb() __asm__ volatile("lfence" ::: "memory")

#define PAGE_SIZE 4096              // Concerned about hugepages?
#define PSAMPLE_SIZE 8 * PAGE_SIZE  // TODO check for high-volume
#define PSAMPLE_DEFAULT_WAKEUP 1000 // sample frequency check

// Basically copypasta from Linux v5.4 includes/linux/perf_event.h
struct perf_callchain_entry {
  uint64_t nr;
  uint64_t ip[];
};

struct perf_callchain_entry_ctx {
  struct perf_callchain_entry *entry;
  uint32_t max_stack;
  uint32_t nr;
  short contexts;
  char contexts_maxed;
};
//</copypasta>

typedef struct PEvent {
  int fd;
  struct perf_event_mmap_page *region;
} PEvent;

// TODO, this comes from BP, SP, and IP
// see arch/x86/include/uapi/asm/perf_regs.h in the linux sources
// We're going to hardcode everything for now...
#define PERF_REGS_MASK ((1 << 6) | (1 << 7) | (1 << 8))

#define u64 uint64_t
#define u32 uint32_t
#define u16 uint16_t
#define u8 uint8_t
#define bool char
#define MAX_INSN 16
typedef struct read_format {
  u64 value;        // The value of the event
  u64 time_enabled; // if PERF_FORMAT_TOTAL_TIME_ENABLED
  u64 time_running; // if PERF_FORMAT_TOTAL_TIME_RUNNING
  //  u64 id;            // if PERF_FORMAT_ID
} read_format;

// Here are some defines to help automatically generate a parametrized family of
// structs for use in parsing samples.  This is largely irrelevant right now,
// but maybe having it here will save someone the headache later.
// counterpoint: no reason to do this since many modes encode a variable-length
// array...
#define PS_X_IDENTIFIER uint64_t sample_id;
#define PS_X_IP uint64_t ip;
#define PS_X_TID uint32_t pid, tid;
#define PS_X_TIME uint64_t time;
#define PS_X_ADDR uint64_t addr;
#define PS_X_ID uint64_t id;
#define PS_X_STREAM_ID uint64_t stream_id;
#define PS_X_CPU uint32_t cpu, res;
#define PS_X_PERIOD uint64_t period;
#define PS_X_READ                                                              \
  struct {                                                                     \
    ...                                                                        \
  };                                                                           \
  \\ TODO
#define PS_X_CALLCHAIN                                                         \
  uint64_t nr;                                                                 \
  uint64_t ips[1];
#define PS_X_RAW                                                               \
  uint32_t size;                                                               \
  char data[1];
#define PS_X_BRANCH_STACK                                                      \
  uint64_t bnr;                                                                \
  struct perf_brancH_entry lbr[1]; // TODO
#define PS_X_REGS_USER                                                         \
  uint64_t abi;                                                                \
  uint64_t regs[0];
#define PS_X_STACK_USER                                                        \
  uint64_t size;                                                               \
  char data[0];                                                                \
  uint64_t dyn_size;
#define PS_X_WEIGHT uint64_t weight;
#define PS_X_DATA_SRC uint64_t data_src;
#define PS_X_TRANSACTION uint64_t transaction;
#define PS_X_REGS_INTR                                                         \
  uint64_t abi;                                                                \
  uint64_t regs[0];

typedef struct sample_id {
  u32 pid, tid; /* if PERF_SAMPLE_TID set */
  u64 time;     /* if PERF_SAMPLE_TIME set */
  //  u64 id;         /* if PERF_SAMPLE_ID set */
  //  u64 stream_id;  /* if PERF_SAMPLE_STREAM_ID set  */
  //  u32 cpu, res;   /* if PERF_SAMPLE_CPU set */
  //  u64 id;         /* if PERF_SAMPLE_IDENTIFIER set */
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
  u32 pid, ppid;
  u32 tid, ptid;
  u64 time;
  struct sample_id sample_id;
} perf_event_fork;

typedef struct perf_event_mmap {
  struct perf_event_header header;
  u32 pid, tid;
  u64 addr;
  u64 len;
  u64 pgoff;
  char filename[];
} perf_event_mmap;

#define PERF_SAMPLE_STACK_SIZE 4096
#define PERF_SAMPLE_STACK_REGS 3
// clang-format off
typedef struct perf_event_sample {
  struct perf_event_header header;
  //  u64    sample_id;                  // if PERF_SAMPLE_IDENTIFIER
  u64 ip;       // if PERF_SAMPLE_IP
  u32 pid, tid; // if PERF_SAMPLE_TID
  u64 time;     // if PERF_SAMPLE_TIME
//  u64    addr;                       // if PERF_SAMPLE_ADDR
//  u64    id;                         // if PERF_SAMPLE_ID
  //  u64    stream_id;                  // if PERF_SAMPLE_STREAM_ID
  //  u32    cpu, res;                   // if PERF_SAMPLE_CPU
  u64 period; // if PERF_SAMPLE_PERIOD
//  struct read_format v;              // if PERF_SAMPLE_READ
//  u64    nr;                         // if PERF_SAMPLE_CALLCHAIN
//  u64    ips[];                      // if PERF_SAMPLE_CALLCHAIN
//  u32    size;                       // if PERF_SAMPLE_RAW
//  char  data[size];                  // if PERF_SAMPLE_RAW
  //  u64    bnr;                        // if PERF_SAMPLE_BRANCH_STACK
  //  struct perf_branch_entry lbr[bnr]; // if PERF_SAMPLE_BRANCH_STACK
  u64 abi;                           // if PERF_SAMPLE_REGS_USER
  u64 regs[PERF_SAMPLE_STACK_REGS];  // if PERF_SAMPLE_REGS_USER
  u64 size;                          // if PERF_SAMPLE_STACK_USER
  char data[PERF_SAMPLE_STACK_SIZE]; // if PERF_SAMPLE_STACK_USER
  u64 dyn_size;                      // if PERF_SAMPLE_STACK_USER
  //  u64    weight;                     // if PERF_SAMPLE_WEIGHT
  //  u64    data_src;                   // if PERF_SAMPLE_DATA_SRC
  //  u64    transaction;                // if PERF_SAMPLE_TRANSACTION
  //  u64    abi;                        // if PERF_SAMPLE_REGS_INTR
  //  u64    regs[weight(mask)];         // if PERF_SAMPLE_REGS_INTR
} perf_event_sample;
// clang-format on

typedef struct perf_samplestacku {
  u64 size;
  char data[];
  // u64    dyn_size;   // Don't forget!
} perf_samplestacku;

struct perf_event_attr g_dd_native_attr = {
    .size = sizeof(struct perf_event_attr),
    .type = PERF_TYPE_SOFTWARE,
    .config = PERF_COUNT_SW_TASK_CLOCK, // If it's good enough for perf(1), it's
                                        // good enough for me!
    .sample_period  = 10000000,         // Who knows!
//    .sample_freq = 1000,
//    .freq = 1,
    .sample_type = PERF_SAMPLE_STACK_USER | PERF_SAMPLE_REGS_USER |
        PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME |
        PERF_SAMPLE_PERIOD,
    .precise_ip = 2, // Change this when moving from SW->HW clock
    .disabled = 0,
    .inherit = 1,
    .inherit_stat = 1,
    .mmap = 1, // keep track of executable mappings
    .task = 1, // Follow fork/stop events
    .enable_on_exec = 1,
    .sample_stack_user = PERF_SAMPLE_STACK_SIZE, // Is this an insane default?
    .sample_regs_user = PERF_REGS_MASK,
    //    .read_format    = //PERF_FORMAT_TOTAL_TIME_ENABLED |
    //                      PERF_FORMAT_TOTAL_TIME_RUNNING,
    //    .exclude_kernel = 1,                        // For a start
    //    (exclude_hv???) .exclude_callchain_kernel = 1, .exclude_hv = 1,
    //    .exclude_idle   = 0,                        // Does this matter???
    .watermark = 1,
    .wakeup_watermark =
        1, // If we've used up 1/4 of our buffer, might as well go for it
};

char perfopen(pid_t pid, PEvent *pe, struct perf_event_attr *attr) {
  if (!attr)
    attr = &g_dd_native_attr;
  pe->fd =
      syscall(__NR_perf_event_open, attr, pid, 0, -1, PERF_FLAG_FD_CLOEXEC);
  if (-1 == pe->fd && EACCES == errno) {
    printf("EACCESS error when calling perf_event_open.\n");
    return -1;
  } else if (-1 == pe->fd) {
    printf("Unspecified error when calling perf_event_open.\n");
    return -1;
  }

  // OK, now populate the page.  TODO what happens when hugepages is enabled,
  // though?
  pe->region = mmap(NULL, PAGE_SIZE + PSAMPLE_SIZE, PROT_READ | PROT_WRITE,
                    MAP_SHARED, pe->fd, 0);
  if (MAP_FAILED == pe->region) {
    printf("mmap error when initiating the perf_event region.\n");
    return -1;
  }

  // Make sure that SIGPROF is delivered to me instead of the called application
  fcntl(pe->fd, F_SETFL, O_ASYNC);
  fcntl(pe->fd, F_SETSIG, SIGPROF);
  fcntl(pe->fd, F_SETOWN_EX, &(struct f_owner_ex){F_OWNER_TID, getpid()});
  //  fcntl(pe->fd, F_SETOWN, (int){gettid()});

  // Ignore the signal
  sigaction(SIGPROF, &(struct sigaction){SIG_IGN}, NULL);

  // Block the signal
  sigset_t sigmask = {0};
  sigemptyset(&sigmask);
  sigaddset(&sigmask, SIGPROF);
  sigprocmask(SIG_BLOCK, &sigmask, NULL);

  // Enable the event
  ioctl(pe->fd, PERF_EVENT_IOC_RESET, 0);
  ioctl(pe->fd, PERF_EVENT_IOC_ENABLE, 1);

  //  ioctl(fd, PERF_EVENT_IOC_REFRESH, 1);

  return 0;
}

void default_callback(struct perf_event_header *hdr, void *arg) {
  (void)arg; // no idea how to use this!
  struct perf_event_sample *pes;
  struct perf_event_mmap *pem;
  switch (hdr->type) {
  case PERF_RECORD_SAMPLE:
    pes = (struct perf_event_sample *)hdr;
    //    printf("NR:  %ld\n", pes->nr);
    break;
  case PERF_RECORD_MMAP:
    pem = (struct perf_event_mmap *)hdr;
    //    printf("MMAP filename: %s\n", pem->filename);
  }
}

typedef struct RingBuffer {
  const char *start;
  unsigned long offset;
} RingBuffer;

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

void main_loop(PEvent *pe,
               void (*event_callback)(struct perf_event_header *, void *),
               void *callback_arg) {
  struct pollfd pfd = {.fd = pe->fd, .events = POLLIN | POLLERR | POLLHUP};
  if (!event_callback)
    event_callback = default_callback;
  ioctl(pe->fd, PERF_EVENT_IOC_ENABLE, 1);
  while (1) {
    poll(&pfd, 1, PSAMPLE_DEFAULT_WAKEUP);
    if (pfd.revents & POLLHUP) {
      printf("Instrumented process died (exiting)\n");
      exit(-1);
    }
    uint64_t head = pe->region->data_head & (PSAMPLE_SIZE - 1);
    uint64_t tail = pe->region->data_tail & (PSAMPLE_SIZE - 1);
    rmb();

    RingBuffer *rb = &(RingBuffer){0};
    rb_init(rb, pe->region);

    int elems = 0;
    while (head > tail) {
      elems++;
      struct perf_event_header *hdr = rb_seek(rb, tail);

      event_callback(hdr, callback_arg);

      tail += hdr->size;
      tail = tail & (PSAMPLE_SIZE - 1);
    }
    pe->region->data_tail = head;
  }
}

#endif // _H_perf
