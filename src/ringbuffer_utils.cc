// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ringbuffer_utils.hpp"

#include "ddres_helpers.hpp"
#include "ipc.hpp"
#include "pevent.hpp"
#include "pevent_lib.hpp"
#include "syscalls.hpp"

#include <sys/eventfd.h>

namespace ddprof {

DDRes ring_buffer_attach(const RingBufferInfo &info, PEvent *pevent) {
  pevent->fd = info.event_fd;
  pevent->mapfd = info.ring_fd;
  pevent->ring_buffer_size = info.mem_size;
  switch (info.ring_buffer_type) {
  case static_cast<int>(RingBufferType::kPerfRingBuffer):
    pevent->ring_buffer_type = RingBufferType::kPerfRingBuffer;
    break;
  case static_cast<int>(RingBufferType::kMPSCRingBuffer):
    pevent->ring_buffer_type = RingBufferType::kMPSCRingBuffer;
    break;
  default:
    return ddres_error(DD_WHAT_PERFRB);
  }
  pevent->custom_event = true;
  return pevent_mmap_event(pevent);
}

DDRes ring_buffer_attach(PEvent &event) { return pevent_mmap_event(&event); }

DDRes ring_buffer_create(size_t buffer_size_page_order,
                         RingBufferType ring_buffer_type, bool custom_event,
                         PEvent *pevent) {
  size_t const buffer_size = perf_mmap_size(buffer_size_page_order);
  pevent->mapfd = memfd_create("allocation_ring_buffer", 1U /*MFD_CLOEXEC*/);
  if (pevent->mapfd == -1) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Error calling memfd_create on watcher %d (%s)",
                           pevent->watcher_pos, strerror(errno));
  }
  if (ftruncate(pevent->mapfd, buffer_size) == -1) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Error calling ftruncate on watcher %d (%s)",
                           pevent->watcher_pos, strerror(errno));
  }
  pevent->fd = eventfd(0, 0);
  if (pevent->fd == -1) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Error calling evenfd on watcher %d (%s)",
                           pevent->watcher_pos, strerror(errno));
  }
  pevent->custom_event = custom_event;
  pevent->ring_buffer_type = ring_buffer_type;
  pevent->ring_buffer_size = buffer_size;
  return {};
}

DDRes ring_buffer_close(PEvent &pevent) { return pevent_close_event(&pevent); }

DDRes ring_buffer_detach(PEvent &event) { return pevent_munmap_event(&event); }

DDRes ring_buffer_setup(size_t buffer_size_page_order,
                        RingBufferType ring_buffer_type, bool custom_event,
                        PEvent *event) {
  DDRES_CHECK_FWD(ring_buffer_create(buffer_size_page_order, ring_buffer_type,
                                     custom_event, event));
  DDRES_CHECK_FWD(ring_buffer_attach(*event));
  return {};
}

DDRes ring_buffer_cleanup(PEvent &event) {
  DDRes const res = ring_buffer_detach(event);
  DDRes const res2 = ring_buffer_close(event);

  return !IsDDResOK(res) ? res : res2;
}

} // namespace ddprof
