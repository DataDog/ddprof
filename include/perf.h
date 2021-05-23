#ifndef _H_perf
#define _H_perf

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

/* TODO
    - Check support/permissions by reading /proc/sys/kernel/perf_event_paranoid
*/

#define rmb() __asm__ volatile("lfence" ::: "memory")

#define PAGE_SIZE 4096               // Concerned about hugepages?
#define PSAMPLE_SIZE 128 * PAGE_SIZE // TODO check for high-volume
#define PSAMPLE_DEFAULT_WAKEUP 1000  // sample frequency check
#define PERF_SAMPLE_STACK_SIZE 16384
#define PERF_SAMPLE_STACK_REGS 3
#define MAX_INSN 16

typedef struct PEvent {
  int pos; // Index into the sample
  int fd;  // Underlying perf event FD
  struct perf_event_mmap_page *region;
} PEvent;

// TODO, this comes from BP, SP, and IP
// see arch/x86/include/uapi/asm/perf_regs.h in the linux sources
// We're going to hardcode everything for now...
#define PERF_REGS_MASK ((1 << 6) | (1 << 7) | (1 << 8))

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
  char bp_type;
} PerfOption;

struct perf_event_attr g_dd_native_attr = {
    .size = sizeof(struct perf_event_attr),
    .sample_type = PERF_SAMPLE_STACK_USER | PERF_SAMPLE_REGS_USER |
        PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME |
        PERF_SAMPLE_PERIOD,
    .precise_ip = 2,
    .disabled = 0,
    .inherit = 1,
    .inherit_stat = 0,
    .mmap = 0, // keep track of executable mappings
    .task = 0, // Follow fork/stop events
    .enable_on_exec = 1,
    .sample_stack_user = PERF_SAMPLE_STACK_SIZE, // Is this an insane default?
    .sample_regs_user = PERF_REGS_MASK,
    .exclude_kernel = 1,
    .exclude_hv = 1,
};

bool sendfail(int sfd) {
  // The call to getfd() checks message metadata upon receipt, so to send a
  // failure it suffices to merely not send SOL_SOCKET with SCM_RIGHTS.
  // This may be a little confusing...  TODO is there a paradoxical setting?
  struct msghdr msg = {
      .msg_iov = &(struct iovec){.iov_base = (char[8]){"!"}, .iov_len = 8},
      .msg_iovlen = 1,
      .msg_control = (char[CMSG_SPACE(sizeof(int))]){0},
      .msg_controllen = CMSG_SPACE(sizeof(int))};
  CMSG_FIRSTHDR(&msg)->cmsg_level = IPPROTO_IP;
  CMSG_FIRSTHDR(&msg)->cmsg_type = IP_PKTINFO;
  CMSG_FIRSTHDR(&msg)->cmsg_len = CMSG_LEN(sizeof(int));
  *((int *)CMSG_DATA(CMSG_FIRSTHDR(&msg))) = -1;

  msg.msg_controllen = CMSG_SPACE(sizeof(int));

  while (sizeof(char[2]) != sendmsg(sfd, &msg, MSG_NOSIGNAL)) {
    if (errno != EINTR)
      return false;
  }
  return true;
}

bool sendfd(int sfd, int fd) {
  struct msghdr *msg = &(struct msghdr){
      .msg_iov = &(struct iovec){.iov_base = (char[8]){"!"}, .iov_len = 8},
      .msg_iovlen = 1,
      .msg_control = (char[CMSG_SPACE(sizeof(int))]){0},
      .msg_controllen = CMSG_SPACE(sizeof(int))};
  CMSG_FIRSTHDR(msg)->cmsg_level = SOL_SOCKET;
  CMSG_FIRSTHDR(msg)->cmsg_type = SCM_RIGHTS;
  CMSG_FIRSTHDR(msg)->cmsg_len = CMSG_LEN(sizeof(int));
  *((int *)CMSG_DATA(CMSG_FIRSTHDR(msg))) = fd;

  msg->msg_controllen = CMSG_SPACE(sizeof(int));

  while (sizeof(char[2]) != sendmsg(sfd, msg, MSG_NOSIGNAL)) {
    if (errno != EINTR)
      return false;
  }
  return true;
}

int getfd(int sfd) {
  struct msghdr msg = {
      .msg_iov = &(struct iovec){.iov_base = (char[8]){"!"}, .iov_len = 8},
      .msg_iovlen = 1,
      .msg_control = (char[CMSG_SPACE(sizeof(int))]){0},
      .msg_controllen = CMSG_SPACE(sizeof(int))};
  while (sizeof(char[8]) != recvmsg(sfd, &msg, MSG_NOSIGNAL)) {
    if (errno != EINTR)
      return -1;
  }

  // Check
  if (CMSG_FIRSTHDR(&msg) && CMSG_FIRSTHDR(&msg)->cmsg_level == SOL_SOCKET &&
      CMSG_FIRSTHDR(&msg)->cmsg_type == SCM_RIGHTS) {
    return *((int *)CMSG_DATA(CMSG_FIRSTHDR(&msg)));
  }
  return -2;
}

int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu, int gfd,
                    unsigned long flags) {
  return syscall(__NR_perf_event_open, attr, pid, cpu, gfd, flags);
}

int perfopen(pid_t pid, PerfOption *opt, int cpu, bool extras) {
  struct perf_event_attr attr = g_dd_native_attr;
  attr.type = opt->type;
  attr.config = opt->config;
  attr.sample_period = opt->sample_period;
  attr.exclude_kernel = !(opt->include_kernel);

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
  // Probably assumes it is being called by the profiler!
  void *region;
  // TODO how to deal with hugepages?
  region = mmap(NULL, PAGE_SIZE + PSAMPLE_SIZE, PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, 0);
  if (!region || MAP_FAILED == region)
    return NULL;

  // Make sure that SIGPROF is delivered to me instead of the called application
  fcntl(fd, F_SETFL, O_ASYNC);
  fcntl(fd, F_SETSIG, SIGPROF);
  fcntl(fd, F_SETOWN_EX, &(struct f_owner_ex){F_OWNER_TID, getpid()});

  // Ignore the signal
  sigaction(SIGPROF, &(struct sigaction){SIG_IGN}, NULL);

  // Block the signal
  sigset_t sigmask = {0};
  sigemptyset(&sigmask);
  sigaddset(&sigmask, SIGPROF);
  sigprocmask(SIG_BLOCK, &sigmask, NULL);

  // Enable the event
  ioctl(fd, PERF_EVENT_IOC_RESET, 0);
  ioctl(fd, PERF_EVENT_IOC_ENABLE, 1);

  return region;
}

void default_callback(struct perf_event_header *hdr, int type, void *arg) {
  struct perf_event_sample *pes;
  struct perf_event_mmap *pem;
  (void)arg;
  (void)pes;
  (void)pem;
  (void)type;
  switch (hdr->type) {
  case PERF_RECORD_SAMPLE:
    pes = (struct perf_event_sample *)hdr;
    (void)pes;
    //    printf("NR:  %ld\n", pes->nr);
    break;
  case PERF_RECORD_MMAP:
    pem = (struct perf_event_mmap *)hdr;
    (void)pem;
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

void main_loop(PEvent *pes, int pe_len,
               void (*event_callback)(struct perf_event_header *, int, void *),
               void *callback_arg) {
  struct pollfd pfd[100];

  // Set to default callback if needed
  // TODO is this really a great idea?
  if (!event_callback)
    event_callback = default_callback;
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
    if (-1 == n) {
      return;
    }
    for (int i = 0; i < pe_len; i++) {
      if (!pfd[i].revents)
        continue;
      if (pfd[i].revents & POLLHUP) {
        return;
      }
      uint64_t head = pes[i].region->data_head & (PSAMPLE_SIZE - 1);
      uint64_t tail = pes[i].region->data_tail & (PSAMPLE_SIZE - 1);

      rmb();

      RingBuffer *rb = &(RingBuffer){0};
      rb_init(rb, pes[i].region);

      int elems = 0;
      while (head > tail) {
        elems++;
        struct perf_event_header *hdr = rb_seek(rb, tail);

        event_callback(hdr, pes[i].pos, callback_arg);

        tail += hdr->size;
        tail = tail & (PSAMPLE_SIZE - 1);
      }
      pes[i].region->data_tail = head;
    }
  }
}

#endif // _H_perf
