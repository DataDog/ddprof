// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres_exception.hpp"
#include "ipc.hpp"
#include "pevent.hpp"
#include "ringbuffer_utils.hpp"

namespace ddprof {
class RingBufferHolder {
public:
  explicit RingBufferHolder(size_t buffer_size_order) : _pevent{} {
    DDRES_CHECK_THROW_EXCEPTION(
        ddprof::ring_buffer_setup(buffer_size_order, &_pevent));
  }

  DDRes mmap() { return pevent_mmap_event(&_pevent); }
  DDRes munmap() { return pevent_munmap_event(&_pevent); }

  ~RingBufferHolder() { ring_buffer_cleanup(_pevent); }

  RingBufferInfo get_buffer_info() const {
    return {static_cast<int64_t>(_pevent.rb.size), _pevent.mapfd, _pevent.fd};
  }

  RingBuffer &get_ring_buffer() { return _pevent.rb; }

  PEvent *get_pevent() { return &_pevent; }

private:
  PEvent _pevent;
};
} // namespace ddprof