#pragma once

#include "perf.h"

typedef struct RingBuffer {
  const char *start;
  unsigned long offset;
  size_t mask;
  size_t meta_size; // size of the metadata page
  size_t size;      // size of the entire region, including metadata
  struct perf_event_mmap_page *region;
  size_t reg_size;
  unsigned char *wrbuf;
} RingBuffer;

bool rb_init(RingBuffer *rb, struct perf_event_mmap_page *page, size_t size);
void rb_free(RingBuffer *rb);
void rb_clear(RingBuffer *rb); // does not deallocate the buffer storage
uint64_t rb_next(RingBuffer *rb);
struct perf_event_header *rb_seek(RingBuffer *rb, uint64_t offset);
bool samp2hdr(struct perf_event_header *hdr, perf_event_sample *sample,
              size_t sz_hdr, uint64_t mask);
perf_event_sample *hdr2samp(struct perf_event_header *hdr, uint64_t mask);

uint64_t hdr_time(struct perf_event_header *hdr, uint64_t mask);
