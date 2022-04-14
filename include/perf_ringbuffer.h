// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "perf.h"

typedef struct RingBuffer {
  const char *start;
  unsigned long offset;
  size_t mask;
  size_t meta_size; // size of the metadata page
  size_t data_size; // size of data pages
  size_t size;      // size of the entire region (data + metadata)
  struct perf_event_mmap_page *region;
  bool is_mirrored;
  unsigned char *wrbuf;
} RingBuffer;

bool rb_init(RingBuffer *rb, struct perf_event_mmap_page *page, size_t size,
             bool is_mirrored);
void rb_free(RingBuffer *rb);
void rb_clear(RingBuffer *rb); // does not deallocate the buffer storage
uint64_t rb_next(RingBuffer *rb);
struct perf_event_header *rb_seek(RingBuffer *rb, uint64_t offset);
bool samp2hdr(struct perf_event_header *hdr, perf_event_sample *sample,
              size_t sz_hdr, uint64_t mask);
perf_event_sample *hdr2samp(const struct perf_event_header *hdr, uint64_t mask);

uint64_t hdr_time(struct perf_event_header *hdr, uint64_t mask);
