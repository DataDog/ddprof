// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ringbuffer_utils.hpp"

#include "ddres_helpers.h"
#include "ipc.hpp"
#include "pevent.h"
#include "pevent_lib.h"
#include "syscalls.hpp"

#include <sys/eventfd.h>

namespace ddprof {

DDRes ring_buffer_attach(const RingBufferInfo &info, PEvent *pevent) {
  pevent->fd = info.event_fd;
  pevent->mapfd = info.ring_fd;
  pevent->ring_buffer_size = info.mem_size;
  pevent->custom_event = true;
  return pevent_mmap_event(pevent);
}

DDRes ring_buffer_attach(PEvent &pe) { return pevent_mmap_event(&pe); }

DDRes ring_buffer_create(size_t buffer_size_page_order, PEvent *pevent) {
  size_t buffer_size = perf_mmap_size(buffer_size_page_order);
  pevent->mapfd =
      ddprof::memfd_create("allocation_ring_buffer", 1U /*MFD_CLOEXEC*/);
  if (pevent->mapfd == -1) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Error calling memfd_create on watcher %d (%s)",
                           pevent->pos, strerror(errno));
  }
  if (ftruncate(pevent->mapfd, buffer_size) == -1) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Error calling ftruncate on watcher %d (%s)",
                           pevent->pos, strerror(errno));
  }
  pevent->fd = eventfd(0, 0);
  if (pevent->fd == -1) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Error calling evenfd on watcher %d (%s)",
                           pevent->pos, strerror(errno));
  }
  pevent->custom_event = true;
  pevent->ring_buffer_size = buffer_size;
  return {};
}

DDRes ring_buffer_close(PEvent &pevent) { return pevent_close_event(&pevent); }

DDRes ring_buffer_detach(PEvent &event) { return pevent_munmap_event(&event); }

DDRes ring_buffer_setup(size_t buffer_size_page_order, PEvent *event) {
  DDRES_CHECK_FWD(ring_buffer_create(buffer_size_page_order, event));
  DDRES_CHECK_FWD(ring_buffer_attach(*event));
  return {};
}

DDRes ring_buffer_cleanup(PEvent &event) {
  DDRes res = ring_buffer_detach(event);
  DDRes res2 = ring_buffer_close(event);

  return !IsDDResOK(res) ? res : res2;
}

} // namespace ddprof
