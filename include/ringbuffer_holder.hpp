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
  explicit RingBufferHolder(size_t buffer_size_order,
                            RingBufferType ring_buffer_type,
                            bool custom_event = true)
      : _pevent{} {
    DDRES_CHECK_THROW_EXCEPTION(ddprof::ring_buffer_setup(
        buffer_size_order, ring_buffer_type, custom_event, &_pevent));
  }

  ~RingBufferHolder() { ring_buffer_cleanup(_pevent); }

  RingBufferHolder(const RingBufferHolder &) = delete;
  RingBufferHolder &operator=(const RingBufferHolder &) = delete;

  RingBufferInfo get_buffer_info() const {
    return {static_cast<int64_t>(_pevent.ring_buffer_size), _pevent.mapfd,
            _pevent.fd, static_cast<int>(_pevent.ring_buffer_type)};
  }

  RingBuffer &get_ring_buffer() { return _pevent.rb; }

private:
  PEvent _pevent;
};
} // namespace ddprof