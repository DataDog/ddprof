// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

extern "C" {
#include "perf_ringbuffer.h"
}

#include <cassert>

namespace ddprof {

class RingBufferWriter {
public:
  explicit RingBufferWriter(RingBuffer &rb) : _rb(rb) {
    _tail = __atomic_load_n(&_rb.region->data_tail, __ATOMIC_ACQUIRE);
    _head = _rb.region->data_head;
    assert(_tail <= _head);
    _available_size = _rb.data_size - (_head - _tail);
  }

  ~RingBufferWriter() {
    __atomic_store_n(&_rb.region->data_head, _head, __ATOMIC_RELEASE);
  }

  RingBufferWriter(const RingBufferWriter &) = delete;
  RingBufferWriter &operator=(const RingBufferWriter &) = delete;

  inline size_t available_size() const { return _available_size; }

  Buffer reserve(size_t n) {
    assert(n < _available_size);
    uint64_t head_linear = _head & _rb.mask;
    std::byte *dest = (std::byte *)(_rb.start + head_linear);
    _available_size -= n;
    _head += n;

    return {dest, n};
  }

  void write(Buffer buf) {
    assert(buf.size() <= _available_size);

    uint64_t head_linear = _head & _rb.mask;
    char *dest = const_cast<char *>(_rb.start) + head_linear;

    memcpy(dest, buf.data(), buf.size());

    _available_size -= buf.size();
    _head += buf.size();
  }

private:
  RingBuffer &_rb;
  uint64_t _tail;
  uint64_t _head;
  size_t _available_size;
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

  inline size_t available_size() const { return _head - _tail; }

  ConstBuffer read_all_available() {
    uint64_t tail_linear = _tail & _rb.mask;
    const std::byte *start =
        reinterpret_cast<const std::byte *>(_rb.start + tail_linear);
    size_t n = _head - _tail;
    _tail = _head;
    return {start, n};
  }

private:
  RingBuffer &_rb;
  uint64_t _tail;
  uint64_t _head;
};

} // namespace ddprof