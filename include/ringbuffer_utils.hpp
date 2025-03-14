// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_buffer.hpp"
#include "mpscringbuffer.hpp"
#include "perf_ringbuffer.hpp"

#include <cassert>
#include <cstring>
#include <mutex>

namespace ddprof {

struct PEvent;
struct RingBufferInfo;

inline constexpr size_t kRingBufferAlignment{8};

// Return `x` rounded up to next multiple of `pow2`
// `pow2` must be a power of 2
// Return 0 for x==0 or x > std::numeric_limits<uint64_t>::max() - pow2 + 1
constexpr uint64_t align_up(uint64_t x, uint64_t pow2) {
  assert(pow2 > 0 && (pow2 & (pow2 - 1)) == 0);
  return ((x - 1) | (pow2 - 1)) + 1;
}

// Return `x` rounded down to previous multiple of `pow2`
// `pow2` must be a power of 2
constexpr uint64_t align_down(uint64_t x, uint64_t pow2) {
  assert(pow2 > 0 && (pow2 & (pow2 - 1)) == 0);
  return x & ~(pow2 - 1);
}

class PerfRingBufferWriter {
public:
  explicit PerfRingBufferWriter(RingBuffer *rb) : _rb(rb) {
    assert(_rb->type == RingBufferType::kPerfRingBuffer);
    _head = _initial_head = *_rb->writer_pos;
    update_available();
    assert(_tail <= _head);
  }

  ~PerfRingBufferWriter() {
    if (_initial_head != _head) {
      commit_internal();
    }
  }

  PerfRingBufferWriter(const PerfRingBufferWriter &) = delete;
  PerfRingBufferWriter &operator=(const PerfRingBufferWriter &) = delete;

  size_t update_available() {
    _tail = __atomic_load_n(_rb->reader_pos, __ATOMIC_ACQUIRE);
    return available_size();
  }

  // implicitly inlined
  [[nodiscard]] size_t available_size() const {
    // Always leave one free char, as a completely full buffer is
    // indistinguishable from an empty one
    return _rb->mask - (_head - _tail);
  }

  Buffer reserve(size_t n) {
    // Make sure to keep samples 8-byte aligned
    size_t const n_aligned = align_up(n, kRingBufferAlignment);
    if (n_aligned == 0 || n_aligned > available_size()) {
      return {};
    }

    uint64_t const head_linear = _head & _rb->mask;
    std::byte *dest = _rb->data + head_linear;
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

  // Return true if notification to consumer is necessary
  // Notification is necessary only if consumer has caught up with producer
  // (meaning tail after commit is at or after head before commit)
  bool commit() {
    commit_internal();
    update_available();
    bool const consumer_has_caught_up = _tail >= _initial_head;
    _initial_head = _head;
    return consumer_has_caught_up;
  }

private:
  void commit_internal() const {
    __atomic_store_n(_rb->writer_pos, _head, __ATOMIC_RELEASE);
  }

  RingBuffer *_rb;
  uint64_t _tail;
  uint64_t _initial_head;
  uint64_t _head;
};

class PerfRingBufferReader {
public:
  explicit PerfRingBufferReader(RingBuffer *rb) : _rb(rb) {
    assert(_rb->type == RingBufferType::kPerfRingBuffer);
    update_available();
    assert(rb->intermediate_reader_pos <= _head);
    assert(_rb->intermediate_reader_pos == *_rb->reader_pos);
  }

  ~PerfRingBufferReader() {
    if (_rb && *_rb->reader_pos < _rb->intermediate_reader_pos) {
      advance();
    }
  }

  PerfRingBufferReader(const PerfRingBufferReader &) = delete;
  PerfRingBufferReader &operator=(const PerfRingBufferReader &) = delete;
  // implicitly inlined
  [[nodiscard]] size_t available_size() const {
    return _head - _rb->intermediate_reader_pos;
  }

  ConstBuffer read_all_available() {
    auto const current_tail = _rb->intermediate_reader_pos;
    uint64_t const tail_linear = current_tail & _rb->mask;
    const std::byte *start = _rb->data + tail_linear;
    size_t const n = _head - current_tail;
    _rb->intermediate_reader_pos = _head;
    return {start, n};
  }

  // Advance initial read cursor by n bytes, this makes the space available for
  // the writer
  void advance(size_t n) {
    // Need to round up size provided by user to recover actual sample size
    n = align_up(n, kRingBufferAlignment);
    assert(*_rb->reader_pos + n <= _rb->intermediate_reader_pos);
    advance_internal(*_rb->reader_pos + n);
  }

  // Advance initial read cursor up to last read byte, this makes the space
  // available for the writer
  void advance() { advance_internal(_rb->intermediate_reader_pos); }

  size_t update_available() {
    _head = __atomic_load_n(_rb->writer_pos, __ATOMIC_ACQUIRE);
    return available_size();
  }

private:
  void advance_internal(uint64_t new_pos) {
    __atomic_store_n(_rb->reader_pos, new_pos, __ATOMIC_RELEASE);
  }

  RingBuffer *_rb;
  uint64_t _head;
};

struct MPSCRingBufferHeader {
  uint64_t size;
  static constexpr uint64_t k_discard_bit = 1UL << 62;
  static constexpr uint64_t k_busy_bit = 1UL << 63;

  static bool is_busy(uint64_t size) { return size & k_busy_bit; }
  static bool is_discarded(uint64_t size) { return size & k_discard_bit; }

  [[nodiscard]] size_t get_size() const { return size & ~k_discard_bit; }

  void set_busy() { size |= k_busy_bit; }
  void set_discarded() { size |= k_discard_bit; }
  [[nodiscard]] bool is_busy() const { return size & k_busy_bit; }
  [[nodiscard]] bool is_discarded() const { return size & k_discard_bit; }
};

class MPSCRingBufferWriter {
public:
  static constexpr std::chrono::milliseconds k_lock_timeout{100};

  explicit MPSCRingBufferWriter(RingBuffer *rb) : _rb(rb) {
    assert(_rb->type == RingBufferType::kMPSCRingBuffer);
    update_tail();
  }

  void update_tail() {
    _tail = __atomic_load_n(_rb->reader_pos, __ATOMIC_ACQUIRE);
  }

  Buffer reserve(size_t n, bool *timeout = nullptr) const {
    size_t const n2 =
        align_up(n + sizeof(MPSCRingBufferHeader), kRingBufferAlignment);
    if (n2 == 0) {
      return {};
    }

    // \fixme{nsavoire} Not sure if spinlock is the best option here
    std::unique_lock const lock{*_rb->spinlock, k_lock_timeout};
    if (!lock.owns_lock()) {
      // timeout on lock
      if (timeout) {
        *timeout = true;
      }
      return {};
    }

    // No need for atomic operation, since we hold the lock
    uint64_t const writer_pos = *_rb->writer_pos;

    uint64_t const new_writer_pos = writer_pos + n2;
    // Check that there is enough free space
    if (_rb->mask < new_writer_pos - _tail) {
      return {};
    }

    uint64_t const head_linear = writer_pos & _rb->mask;
    auto *hdr =
        reinterpret_cast<MPSCRingBufferHeader *>(_rb->data + head_linear);

    // Mark the sample as busy
    hdr->size = n | MPSCRingBufferHeader::k_busy_bit;

    // Atomic operation required to synchronize with reader load_acquire
    __atomic_store_n(_rb->writer_pos, new_writer_pos, __ATOMIC_RELEASE);

    return {reinterpret_cast<std::byte *>(hdr + 1), n};
  }

  // Return true if notification to consumer is necessary.
  // Notification is necessary only if consumer has caught up with producer
  // (meaning tail after commit is at or after head before commit).
  bool commit(Buffer buf) { return commit_internal(buf, false); }

  bool discard(Buffer buf) { return commit_internal(buf, true); }

private:
  bool commit_internal(Buffer buf, bool discard) {
    MPSCRingBufferHeader *hdr =
        reinterpret_cast<MPSCRingBufferHeader *>(buf.data()) - 1;

    // Clear busy bit
    uint64_t new_size = hdr->size ^ MPSCRingBufferHeader::k_busy_bit;
    if (discard) {
      new_size |= MPSCRingBufferHeader::k_discard_bit;
    }

    // Needs release ordering to make sure that all previous writes are
    // visible to the reader once reader acquires `hdr->size`.
    __atomic_store_n(&hdr->size, new_size, __ATOMIC_RELEASE);

    update_tail();
    uint64_t const tail_linear = _tail & _rb->mask;
    return tail_linear ==
        static_cast<uint64_t>(reinterpret_cast<std::byte *>(hdr) - _rb->data);
  }

  RingBuffer *_rb;
  uint64_t _tail;
};

inline ConstBuffer mpsc_rb_read_sample(RingBuffer &rb, uint64_t head) {
  while (true) {
    auto tail = rb.intermediate_reader_pos;
    if (head == tail) {
      return {};
    }

    uint64_t const tail_linear = tail & rb.mask;
    std::byte *start = rb.data + tail_linear;
    auto *hdr = reinterpret_cast<MPSCRingBufferHeader *>(start);
    uint64_t const sz = __atomic_load_n(&hdr->size, __ATOMIC_ACQUIRE);

    // Sample not committed yet, bail out
    if (MPSCRingBufferHeader::is_busy(sz)) {
      return {};
    }

    rb.intermediate_reader_pos +=
        align_up((sz & ~MPSCRingBufferHeader::k_discard_bit) +
                     sizeof(MPSCRingBufferHeader),
                 kRingBufferAlignment);

    if (MPSCRingBufferHeader::is_discarded(sz)) {
      continue;
    }

    return {reinterpret_cast<std::byte *>(hdr + 1), sz};
  }
}

inline ConstBuffer mpsc_rb_read_sample(RingBuffer &rb) {
  auto head = __atomic_load_n(rb.writer_pos, __ATOMIC_ACQUIRE);
  return mpsc_rb_read_sample(rb, head);
}

inline const perf_event_header *mpsc_rb_read_event(RingBuffer &rb) {
  auto buffer = mpsc_rb_read_sample(rb);
  return reinterpret_cast<const perf_event_header *>(buffer.data());
}

inline void mpsc_rb_advance_if_possible(RingBuffer &rb, const void *event) {
  assert(rb.type == RingBufferType::kMPSCRingBuffer);

  // set current event as discarded
  const_cast<MPSCRingBufferHeader *>(
      reinterpret_cast<const MPSCRingBufferHeader *>(event))[-1]
      .set_discarded();

  uint64_t new_tail = *rb.reader_pos;
  uint64_t const head = __atomic_load_n(rb.writer_pos, __ATOMIC_ACQUIRE);
  uint64_t const mask = rb.mask;

  // loop until we reach head or find a non-discarded event
  while (new_tail < head) {
    uint64_t const tail_linear = new_tail & mask;
    std::byte *start = rb.data + tail_linear;
    auto *hdr = reinterpret_cast<MPSCRingBufferHeader *>(start);
    if (hdr->is_discarded()) {
      new_tail += align_up((hdr->get_size() + sizeof(MPSCRingBufferHeader)),
                           kRingBufferAlignment);
    } else {
      break;
    }
  }

  // update tail
  __atomic_store_n(rb.reader_pos, new_tail, __ATOMIC_RELEASE);
}

inline bool perf_rb_has_inflight_events(const RingBuffer &rb) {
  return rb.intermediate_reader_pos != *rb.reader_pos;
}

inline const perf_event_header *perf_rb_read_event(RingBuffer &rb) {
  auto head = __atomic_load_n(rb.writer_pos, __ATOMIC_ACQUIRE);
  auto tail = rb.intermediate_reader_pos;

  if (tail == head) {
    return nullptr;
  }

  uint64_t const tail_linear = tail & rb.mask;
  auto *hdr = reinterpret_cast<perf_event_header *>(rb.data + tail_linear);
  size_t const sz_sample = hdr->size;

  // align_up might not be needed since perf events should already be 8-byte
  // aligned
  rb.intermediate_reader_pos += align_up(sz_sample, kRingBufferAlignment);
  return hdr;
}

inline void perf_rb_advance(RingBuffer &rb) {
  __atomic_store_n(rb.reader_pos, rb.intermediate_reader_pos, __ATOMIC_RELEASE);
}

class MPSCRingBufferReader {
public:
  explicit MPSCRingBufferReader(RingBuffer *rb) : _rb(rb) {
    assert(_rb->type == RingBufferType::kMPSCRingBuffer);
    update_available();
    assert(rb->intermediate_reader_pos <= _head);
    assert(_rb->intermediate_reader_pos == *_rb->reader_pos);
  }

  ~MPSCRingBufferReader() {
    if (_rb && *_rb->reader_pos < _rb->intermediate_reader_pos) {
      advance();
    }
  }

  MPSCRingBufferReader(const MPSCRingBufferReader &) = delete;
  MPSCRingBufferReader &operator=(const MPSCRingBufferReader &) = delete;

  [[nodiscard]] size_t available_size() const {
    return _head - _rb->intermediate_reader_pos;
  }

  ConstBuffer read_sample() { return mpsc_rb_read_sample(*_rb, _head); }

  // Update ring buffer initial reader pos (usually done by destructor)
  void advance() {
    __atomic_store_n(_rb->reader_pos, _rb->intermediate_reader_pos,
                     __ATOMIC_RELEASE);
  }

  size_t update_available() {
    _head = __atomic_load_n(_rb->writer_pos, __ATOMIC_ACQUIRE);
    return available_size();
  }

private:
  RingBuffer *_rb;
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