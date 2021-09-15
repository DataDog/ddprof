#pragma once

#include <linux/perf_event.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>

#include "ddres_def.h"
#include "perf_option.h"

#define PSAMPLE_DEFAULT_WAKEUP 1000 // sample frequency check
#define PERF_SAMPLE_STACK_SIZE (4096 * 8)
#define PERF_SAMPLE_STACK_REGS 3
#define MAX_INSN 16

// TODO, probably make this part of the unwinding context or ddprof ctx
#define DEFAULT_SAMPLE_TYPE                                                    \
  (PERF_SAMPLE_STACK_USER | PERF_SAMPLE_REGS_USER | PERF_SAMPLE_TID |          \
   PERF_SAMPLE_TIME | PERF_SAMPLE_PERIOD)

// TODO, this comes from BP, SP, and IP
// see arch/x86/include/uapi/asm/perf_regs.h in the linux sources
// We're going to hardcode everything for now...
#define PERF_REGS_MASK ((1 << 6) | (1 << 7) | (1 << 8))

// This is a human-hardcoded number given the mask above; update it if the mask
// gets more bits
#define PERF_REGS_COUNT 3

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
  uint64_t    sample_id;                // if PERF_SAMPLE_IDENTIFIER
  uint64_t    ip;                       // if PERF_SAMPLE_IP
  uint32_t    pid, tid;                 // if PERF_SAMPLE_TID
  uint64_t    time;                     // if PERF_SAMPLE_TIME
  uint64_t    addr;                     // if PERF_SAMPLE_ADDR
  uint64_t    id;                       // if PERF_SAMPLE_ID
  uint64_t    stream_id;                // if PERF_SAMPLE_STREAM_ID
  uint32_t    cpu, res;                 // if PERF_SAMPLE_CPU
  uint64_t    period;                   // if PERF_SAMPLE_PERIOD
  struct      read_format *v;           // if PERF_SAMPLE_READ
  uint64_t    nr;                       // if PERF_SAMPLE_CALLCHAIN
  uint64_t    *ips;                     // if PERF_SAMPLE_CALLCHAIN
  uint32_t    size_raw;                 // if PERF_SAMPLE_RAW
  char        *data_raw;                // if PERF_SAMPLE_RAW
  uint64_t    bnr;                      // if PERF_SAMPLE_BRANCH_STACK
  struct      perf_branch_entry *lbr;   // if PERF_SAMPLE_BRANCH_STACK
  uint64_t    abi;                      // if PERF_SAMPLE_REGS_USER
  uint64_t    *regs;                    // if PERF_SAMPLE_REGS_USER
  uint64_t    size_stack;               // if PERF_SAMPLE_STACK_USER
  char        *data_stack;              // if PERF_SAMPLE_STACK_USER
  uint64_t    dyn_size_stack;           // if PERF_SAMPLE_STACK_USER
  uint64_t    weight;                   // if PERF_SAMPLE_WEIGHT
  uint64_t    data_src;                 // if PERF_SAMPLE_DATA_SRC
  uint64_t    transaction;              // if PERF_SAMPLE_TRANSACTION
  uint64_t    abi_intr;                 // if PERF_SAMPLE_REGS_INTR
  uint64_t    *regs_intr;               // if PERF_SAMPLE_REGS_INTR
} perf_event_sample;
// clang-format on

typedef struct perf_samplestacku {
  uint64_t size;
  char data[];
  // uint64_t    dyn_size;   // Don't forget!
} perf_samplestacku;

// Used by rb_init() and friends
typedef struct RingBuffer {
  const char *start;
  unsigned long offset;
  size_t size;
  size_t mask;
} RingBuffer;

int perf_event_open(struct perf_event_attr *, pid_t, int, int, unsigned long);
int perfopen(pid_t pid, const PerfOption *opt, int cpu, bool extras);
size_t perf_mmap_size(int buf_size_shift);
void *perfown_sz(int fd, size_t size_of_buffer);
void *perfown(int fd, size_t *size);
int perfdisown(void *region, size_t size);
void rb_init(RingBuffer *rb, struct perf_event_mmap_page *page, size_t size);
uint64_t rb_next(RingBuffer *);
struct perf_event_header *rb_seek(RingBuffer *, uint64_t);
perf_event_sample *hdr2samp(struct perf_event_header *hdr);
