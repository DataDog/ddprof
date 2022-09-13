// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf_ringbuffer.hpp"

#include "logger.hpp"
#include "mpscringbuffer.hpp"

#include <stdlib.h>

bool rb_init(RingBuffer *rb, void *base, size_t size,
             RingBufferType ring_buffer_type) {
  rb->meta_size = get_page_size();
  rb->base = base;
  rb->data = reinterpret_cast<std::byte *>(base) + rb->meta_size;
  rb->data_size = size - rb->meta_size;
  rb->mask = get_mask_from_size(size);
  rb->type = ring_buffer_type;
  switch (ring_buffer_type) {
  case RingBufferType::kPerfRingBuffer: {
    perf_event_mmap_page *meta =
        reinterpret_cast<perf_event_mmap_page *>(rb->base);
    rb->reader_pos = reinterpret_cast<uint64_t *>(&meta->data_tail);
    rb->writer_pos = reinterpret_cast<uint64_t *>(&meta->data_head);
    break;
  }
  case RingBufferType::kMPSCRingBuffer: {
    ddprof::MPSCRingBufferMetaDataPage *meta =
        reinterpret_cast<ddprof::MPSCRingBufferMetaDataPage *>(rb->base);
    rb->reader_pos = &meta->reader_pos;
    rb->writer_pos = &meta->writer_pos;
    rb->spinlock = &meta->spinlock;
    break;
  }
  default:
    return false;
  }

  return true;
}

void rb_free(RingBuffer *rb) {}

// aligned block into two 4-byte blocks easier
typedef union flipper {
  uint64_t full;
  uint32_t half[2];
} flipper;

#define SZ_CHECK                                                               \
  if ((sz += 8) >= sz_hdr)                                                     \
  return false
bool samp2hdr(perf_event_header *hdr, const perf_event_sample *sample,
              size_t sz_hdr, uint64_t mask) {
  // There is absolutely no point for this interface except testing

  // Presumes that the user has allocated enough room for the whole sample
  if (!hdr || !sz_hdr || sz_hdr < sizeof(struct perf_event_header))
    return false;
  memset(hdr, 0, sz_hdr);

  // Initiate
  size_t sz = sizeof(*hdr);
  *hdr = sample->header;
  hdr->size = 0; // update size at end

  uint64_t *buf = (uint64_t *)&hdr[1]; // buffer starts at sample

  if (sz >= sz_hdr)
    return false;

  if (PERF_SAMPLE_IDENTIFIER & mask) {
    *buf++ = sample->sample_id;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_IP & mask) {
    *buf++ = sample->ip;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_TID & mask) {
    ((flipper *)buf)->half[0] = sample->pid;
    ((flipper *)buf)->half[1] = sample->tid;
    buf++;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_TIME & mask) {
    *buf++ = sample->time;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_ADDR & mask) {
    *buf++ = sample->addr;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_ID & mask) {
    *buf++ = sample->id;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_STREAM_ID & mask) {
    *buf++ = sample->stream_id;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_CPU & mask) {
    ((flipper *)buf)->half[0] = sample->cpu;
    ((flipper *)buf)->half[1] = sample->res;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_PERIOD & mask) {
    *buf++ = sample->period;
    SZ_CHECK;
  }
  if (PERF_SAMPLE_READ & mask) {
    *((struct read_format *)buf) = *sample->v;
    buf += sizeof(struct read_format) / 8; // read_format is uint64_t's
    sz += sizeof(struct read_format);
    if (sz >= sz_hdr)
      return false;
  }
  if (PERF_SAMPLE_CALLCHAIN & mask) {
    *buf++ = sample->nr;
    SZ_CHECK;

    // Copy the values
    sz += 8 * sample->nr; // early check
    if (sz >= sz_hdr)
      return false;
    memcpy(buf, sample->ips, sample->nr);
    buf += sample->nr;
  }
  if (PERF_SAMPLE_RAW & mask) {
    ((flipper *)buf)->half[0] = sample->size_raw;
    memcpy(&((flipper *)buf)->half[1], sample->data_raw, sample->size_raw);
    buf += 1 + (sample->size_raw / sizeof(*buf));
    SZ_CHECK;
  }
  if (PERF_SAMPLE_BRANCH_STACK & mask) {}
  if (PERF_SAMPLE_REGS_USER & mask) {
    *buf++ = sample->abi;
    SZ_CHECK;

    // Copy the values
    sz += static_cast<size_t>(8 *
                              PERF_REGS_COUNT); // TODO pass this in the watcher
    if (sz >= sz_hdr)
      return false;
    memcpy(buf, sample->regs, PERF_REGS_COUNT);
    buf += PERF_REGS_COUNT;
  }
  if (PERF_SAMPLE_STACK_USER & mask) {
    *buf++ = sample->size_stack;
    SZ_CHECK;

    if (sample->size_stack) {
      sz += sample->size_stack;
      if (sz >= sz_hdr)
        return false;
      memcpy(buf, sample->data_stack, sample->size_stack);
      buf += ((sample->size_stack + 0x7) & ~0x7) / 8; // align and convert
      *buf++ = sample->dyn_size_stack;
      SZ_CHECK;
    }
  }
  if (PERF_SAMPLE_WEIGHT & mask) {}
  if (PERF_SAMPLE_DATA_SRC & mask) {}
  if (PERF_SAMPLE_TRANSACTION & mask) {}
  if (PERF_SAMPLE_REGS_INTR & mask) {}

  hdr->size = sz;
  return true;
}

perf_event_sample *hdr2samp(const perf_event_header *hdr, uint64_t mask) {
  static perf_event_sample sample = {};
  memset(&sample, 0, sizeof(sample));

  sample.header = *hdr;

  uint64_t *buf = (uint64_t *)&hdr[1]; // sample starts after header

  if (PERF_SAMPLE_IDENTIFIER & mask) {
    sample.sample_id = *buf++;
  }
  if (PERF_SAMPLE_IP & mask) {
    sample.ip = *buf++;
  }
  if (PERF_SAMPLE_TID & mask) {
    sample.pid = ((flipper *)buf)->half[0];
    sample.tid = ((flipper *)buf)->half[1];
    buf++;
  }
  if (PERF_SAMPLE_TIME & mask) {
    sample.time = *buf++;
  }
  if (PERF_SAMPLE_ADDR & mask) {
    sample.addr = *buf++;
  }
  if (PERF_SAMPLE_ID & mask) {
    sample.id = *buf++;
  }
  if (PERF_SAMPLE_STREAM_ID & mask) {
    sample.stream_id = *buf++;
  }
  if (PERF_SAMPLE_CPU & mask) {
    sample.cpu = ((flipper *)buf)->half[0];
    sample.res = ((flipper *)buf)->half[1];
    buf++;
  }
  if (PERF_SAMPLE_PERIOD & mask) {
    sample.period = *buf++;
  }
  if (PERF_SAMPLE_READ & mask) {
    sample.v = (struct read_format *)buf++;
  }

  if (PERF_SAMPLE_CALLCHAIN & mask) {
    sample.nr = *buf++;
    sample.ips = buf;
    buf += sample.nr;
  }
  if (PERF_SAMPLE_RAW & mask) {
    // size_raw is a 32-bit integer!
    sample.size_raw = ((flipper *)buf)->half[0];
    sample.data_raw =
        sample.size_raw ? (char *)&((flipper *)buf)->half[1] : NULL;
    buf += 1 + (sample.size_raw / sizeof(*buf)); // Advance + align
  }
  if (PERF_SAMPLE_BRANCH_STACK & mask) {}
  if (PERF_SAMPLE_REGS_USER & mask) {
    sample.abi = *buf++;
    // Since some of the previous events are variable-length, ABI gives us
    // an opportunity to sanity-check the buffer (only 2 legal values!)
    if (sample.abi != PERF_SAMPLE_REGS_ABI_32 &&
        sample.abi != PERF_SAMPLE_REGS_ABI_64) {
      return NULL;
    }
    sample.regs = buf;
    buf += PERF_REGS_COUNT;
  }
  if (PERF_SAMPLE_STACK_USER & mask) {
    uint64_t size_stack = *buf++;

    // Empirically, it seems that the size of the static stack is either 0 or
    // the amount requested in the call to `perf_event_open()`.  We don't
    // check for that, since there isn't much we'd be able to do anyway.
    if (size_stack == 0) {
      sample.size_stack = 0;
      sample.dyn_size_stack = 0;
      sample.data_stack = NULL;
    } else {
      uint64_t dynsz_stack = 0;
      sample.data_stack = (char *)buf;
      buf += ((size_stack + 0x7UL) & ~0x7UL) / 8; // align (/8 as it is uint64)

      // If the size was specified, we also have a dyn_size
      dynsz_stack = *buf++;

      // If the dyn_size is too big, zero out the stack size since it is
      // likely an error
      sample.size_stack = size_stack < dynsz_stack ? 0 : dynsz_stack;
      sample.dyn_size_stack = dynsz_stack; // for debugging
    }
  }
  if (PERF_SAMPLE_WEIGHT & mask) {}
  if (PERF_SAMPLE_DATA_SRC & mask) {}
  if (PERF_SAMPLE_TRANSACTION & mask) {}
  if (PERF_SAMPLE_REGS_INTR & mask) {}

  // Ensure buf can be used in a semantically correct way without worrying
  // whether we've implemented the next consumer.  This is to keep static
  // analysis and checkers happy.
  (void)buf;

  return &sample;
}

inline static int get_bits(uint64_t val) {
  int count = 0;
  while (val) {
    count += val & 1;
    val >>= 1;
  }
  return count;
}

uint64_t hdr_time(struct perf_event_header *hdr, uint64_t mask) {
  if (!(mask & PERF_SAMPLE_TIME))
    return 0;

  uint64_t sampleid_mask_bits;
  uint8_t *buf;

  switch (hdr->type) {

  // For sample events, there is no sample_id struct at the end of the feed,
  // so we need to compute the time from the sample.  Rather than doing the
  // full sample2hdr computation, we do an abbreviated lookup from the top of
  // the header
  case PERF_RECORD_SAMPLE: {
    buf = (uint8_t *)&hdr[1];
    uint64_t mbits = PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_IP | PERF_SAMPLE_TID;
    mbits &= mask;
    return *(uint64_t *)&buf[static_cast<ptrdiff_t>(8 * get_bits(mbits))];
  }
  // For non-sample type events, the time is in the sample_id struct which is
  // at the very end of the feed.  We seek to the top of the header, which
  // requires computing how many values are in the sample_id struct (depends
  // on mask), and then we seek into the sample_id to the time position.  This
  // position is the top of the struct if the PERF_SAMPLE_TID is not given and
  // 8 bytes into the struct if it is.
  case PERF_RECORD_MMAP:
  case PERF_RECORD_MMAP2:
  case PERF_RECORD_COMM:
  case PERF_RECORD_EXIT:
  case PERF_RECORD_FORK:
  case PERF_RECORD_LOST:
    sampleid_mask_bits = mask;
    sampleid_mask_bits &= PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_ID |
        PERF_SAMPLE_STREAM_ID | PERF_SAMPLE_CPU | PERF_SAMPLE_IDENTIFIER;
    sampleid_mask_bits = get_bits(sampleid_mask_bits);
    buf = ((uint8_t *)hdr) + hdr->size - sizeof(uint64_t) * sampleid_mask_bits;
    return *(
        uint64_t *)&buf[static_cast<ptrdiff_t>(8 * !!(mask & PERF_SAMPLE_TID))];
  }

  return 0;
}
