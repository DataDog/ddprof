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

class RingBufferWriter {
public:
  explicit RingBufferWriter(RingBuffer &rb) : _rb(rb) {
    _tail = __atomic_load_n(&_rb.region->data_tail, __ATOMIC_ACQUIRE);
    _head = _initial_head = _rb.region->data_head;
    assert(_tail <= _head);
  }

  ~RingBufferWriter() {
    if (_initial_head != _head) {
      __atomic_store_n(&_rb.region->data_head, _head, __ATOMIC_RELEASE);
    }
  }

  RingBufferWriter(const RingBufferWriter &) = delete;
  RingBufferWriter &operator=(const RingBufferWriter &) = delete;

  inline size_t available_size() const {
    // Always leave one free char, as a completely full buffer is
    // indistinguishable from an empty one
    return _rb.data_size - (_head + 1 - _tail);
  }

  Buffer reserve(size_t n) {
    assert(n <= available_size());

    uint64_t head_linear = _head & _rb.mask;
    std::byte *dest = (std::byte *)(_rb.start + head_linear);
    _head += n;

    return {dest, n};
  }

  void write(Buffer buf) {
    assert(buf.size() <= available_size());

    uint64_t head_linear = _head & _rb.mask;
    char *dest = const_cast<char *>(_rb.start) + head_linear;

    memcpy(dest, buf.data(), buf.size());
    _head += buf.size();
  }

  // return true if notification to consumer is necesssary
  // Notification is necessary only if consumer has caught up with producer
  // (meaning tail afer commit is at or after head before commit)
  bool commit() {
    __atomic_store_n(&_rb.region->data_head, _head, __ATOMIC_RELEASE);
    _tail = __atomic_load_n(&_rb.region->data_tail, __ATOMIC_ACQUIRE);
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

class RingBufferReader {
public:
  explicit RingBufferReader(RingBuffer &rb) : _rb(rb) {
    _head = __atomic_load_n(&rb.region->data_head, __ATOMIC_ACQUIRE);
    _tail = _rb.region->data_tail;
    assert(_tail <= _head);
  }

  ~RingBufferReader() {
    __atomic_store_n(&_rb.region->data_tail, _tail, __ATOMIC_RELEASE);
  }

  inline size_t available_for_read() const { return _head - _tail; }

  ConstBuffer read_all_available() {
    uint64_t tail_linear = _tail & _rb.mask;
    const std::byte *start =
        reinterpret_cast<const std::byte *>(_rb.start + tail_linear);
    size_t n = _head - _tail;
    _tail = _head;
    return {start, n};
  }

  void check_for_new_data() {
    _head = __atomic_load_n(&_rb.region->data_head, __ATOMIC_ACQUIRE);
  }

private:
  RingBuffer &_rb;
  uint64_t _tail;
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
DDRes ring_buffer_create(size_t buffer_size_page_order, PEvent *event);

// Destroy ring buffer: close memfd / eventfd
DDRes ring_buffer_close(PEvent &event);

// Create and attach ring buffer
DDRes ring_buffer_setup(size_t buffer_size_page_order, PEvent *event);

// Unmap and close ring buffer
DDRes ring_buffer_cleanup(PEvent &event);

} // namespace ddprof