// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_buffer.hpp"
#include "perf_ringbuffer.hpp"

#include <cassert>
#include <cstring>

struct PEvent;

namespace ddprof {

struct RingBufferInfo;

// Return `x` rounded up to next multiple of `y`
// `y` must be a power of 2
// Return 0 for x==0 or x > std::numeric_limits<uint64_t>::max()-7
constexpr inline uint64_t round_up_to_mutiple_of_pow2(uint64_t x,
                                                      uint64_t pow2) {
  assert(pow2 > 0 && (pow2 & (pow2 - 1)) == 0);
  return ((x - 1) | (pow2 - 1)) + 1;
}

constexpr inline uint64_t round_down_to_mutiple_of_pow2(uint64_t x,
                                                        uint64_t pow2) {
  assert(pow2 > 0 && (pow2 & (pow2 - 1)) == 0);
  return x & ~(pow2 - 1);
}

class PerfRingBufferWriter {
public:
  explicit PerfRingBufferWriter(RingBuffer &rb) : _rb(rb) {
    assert(rb.type == RingBufferType::kPerfRingBuffer);
    _tail = __atomic_load_n(_rb.reader_pos, __ATOMIC_ACQUIRE);
    _head = _initial_head = *_rb.writer_pos;
    assert(_tail <= _head);
  }

  ~PerfRingBufferWriter() {
    if (_initial_head != _head) {
      __atomic_store_n(_rb.writer_pos, _head, __ATOMIC_RELEASE);
    }
  }

  PerfRingBufferWriter(const PerfRingBufferWriter &) = delete;
  PerfRingBufferWriter &operator=(const PerfRingBufferWriter &) = delete;

  size_t update_available() {
    _tail = __atomic_load_n(_rb.reader_pos, __ATOMIC_ACQUIRE);
    return available_size();
  }

  inline size_t available_size() const {
    // Always leave one free char, as a completely full buffer is
    // indistinguishable from an empty one
    return _rb.mask - (_head - _tail);
  }

  Buffer reserve(size_t n) {
    // Make sure to keep samples 8-byte aligned
    size_t n_aligned = round_up_to_mutiple_of_pow2(n, 8);
    if (n_aligned == 0 || n_aligned > available_size()) {
      return {};
    }

    uint64_t head_linear = _head & _rb.mask;
    std::byte *dest = _rb.data + head_linear;
    _head += n_aligned;

    return {dest, n};
  }

  bool write(ConstBuffer buf) {
    auto dest = reserve(buf.size());
    if (dest.empty()) {
      return false;
    }
    memcpy(dest.data(), buf.data(), buf.size());
    return true;
  }

  // Return true if notification to consumer is necesssary
  // Notification is necessary only if consumer has caught up with producer
  // (meaning tail afer commit is at or after head before commit)
  bool commit() {
    __atomic_store_n(_rb.writer_pos, _head, __ATOMIC_RELEASE);
    _tail = __atomic_load_n(_rb.reader_pos, __ATOMIC_ACQUIRE);
    bool consumer_has_caught_up = _tail >= _initial_head;
    _initial_head = _head;
    return consumer_has_caught_up;
  }

private:
  RingBuffer &_rb;
  uint64_t _tail;
  uint64_t _initial_head;
  uint64_t _head;
};

class PerfRingBufferReader {
public:
  explicit PerfRingBufferReader(RingBuffer &rb) : _rb(rb) {
    assert(rb.type == RingBufferType::kPerfRingBuffer);
    _head = __atomic_load_n(rb.writer_pos, __ATOMIC_ACQUIRE);
    _tail = *_rb.reader_pos;
    _initial_tail = _tail;
    assert(_tail <= _head);
  }

  ~PerfRingBufferReader() {
    if (_initial_tail < _tail)
      __atomic_store_n(_rb.reader_pos, _tail, __ATOMIC_RELEASE);
  }

  inline size_t available_size() const { return _head - _tail; }

  ConstBuffer read_all_available() {
    uint64_t tail_linear = _tail & _rb.mask;
    const std::byte *start = _rb.data + tail_linear;
    size_t n = _head - _tail;
    _tail = _head;
    return {start, n};
  }

  void advance(size_t n) {
    // Need to round up size provided by user to recover actual sample size
    n = round_up_to_mutiple_of_pow2(n, 8);
    assert(_initial_tail + n <= _tail);
    _initial_tail += n;
    __atomic_store_n(_rb.reader_pos, _initial_tail, __ATOMIC_RELEASE);
  }

  void advance() {
    _initial_tail = _tail;
    __atomic_store_n(_rb.reader_pos, _initial_tail, __ATOMIC_RELEASE);
  }

  size_t update_available() {
    _head = __atomic_load_n(_rb.writer_pos, __ATOMIC_ACQUIRE);
    return available_size();
  }

private:
  RingBuffer &_rb;
  uint64_t _tail;
  uint64_t _initial_tail;
  uint64_t _head;
};

// Initialize event from ring buffer info and mmap ring buffer into process
DDRes ring_buffer_attach(const RingBufferInfo &info, PEvent *event);

// Mmap ring buffer into process from already initialized event
DDRes ring_buffer_attach(PEvent &event);

// Unmap ring buffer
DDRes ring_buffer_detach(PEvent &event);

// Create ring buffer (create memfd and eventfd)
// Ring buffer is not mapped upon return from this function, ring_buffer_attach
// needs to be called to map it
DDRes ring_buffer_create(size_t buffer_size_page_order,
                         RingBufferType ring_buffer_type, bool custom_event,
                         PEvent *event);

// Destroy ring buffer: close memfd / eventfd
DDRes ring_buffer_close(PEvent &event);

// Create and attach ring buffer
DDRes ring_buffer_setup(size_t buffer_size_page_order,
                        RingBufferType ring_buffer_type, bool custom_event,
                        PEvent *event);

// Unmap and close ring buffer
DDRes ring_buffer_cleanup(PEvent &event);

} // namespace ddprof