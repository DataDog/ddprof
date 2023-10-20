// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "perf.hpp"

namespace ddprof {

enum class RingBufferType : uint8_t { kPerfRingBuffer, kMPSCRingBuffer };

class SpinLock;

struct RingBuffer {
  RingBufferType type;
  uint64_t mask;
  size_t meta_size; // size of the metadata
  size_t data_size; // size of data
  std::byte *data;
  void *base;

  uint64_t *writer_pos;
  uint64_t *reader_pos;

  // only used for MPSCRingBuffer
  SpinLock *spinlock;
  uint64_t time_zero;
  uint32_t time_mult;
  uint16_t time_shift;
  uint8_t perf_clock_source;
  bool tsc_available;
};

bool rb_init(RingBuffer *rb, void *base, size_t size, RingBufferType type);
void rb_free(RingBuffer *rb);

bool samp2hdr(perf_event_header *hdr, const perf_event_sample *sample,
              size_t sz_hdr, uint64_t mask);

perf_event_sample *hdr2samp(const perf_event_header *hdr, uint64_t mask);

uint64_t hdr_time(const perf_event_header *hdr, uint64_t mask);

} // namespace ddprof
