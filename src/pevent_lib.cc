// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "pevent_lib.hpp"

#include "ddprof_cmdline.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "lib/allocation_event.hpp"
#include "perf.hpp"
#include "ringbuffer_utils.hpp"
#include "sys_utils.hpp"
#include "syscalls.hpp"
#include "tracepoint_config.hpp"
#include "user_override.hpp"

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace ddprof {

namespace {
void display_system_config() {
  int val;
  DDRes const res = sys_perf_event_paranoid(val);
  if (IsDDResOK(res)) {
    LG_WRN("Check System Configuration - perf_event_paranoid=%d", val);
  } else {
    LG_WRN("Unable to access system configuration");
  }
}
} // namespace

int pevent_compute_min_mmap_order(int min_buffer_size_order,
                                  uint32_t stack_sample_size,
                                  unsigned min_number_samples) {
  int ret_order = min_buffer_size_order;
  // perf events and allocation events should be roughly the same size
  size_t const single_event_size = sizeof_allocation_event(stack_sample_size);
  // Ensure we can at least fit 8 samples within one buffer
  while (((perf_mmap_size(ret_order) - get_page_size()) / single_event_size) <
         min_number_samples) {
    ++ret_order;
  }
  return ret_order;
}

DDRes pevent_mmap_event(PEvent *event) {
  if (event->mapfd == -1)
    return {};

  UIDInfo info;
  void *region;
  if (!(region = perfown_sz(event->mapfd, event->ring_buffer_size))) {
    // Switch user if needed (when root switch to nobody user)
    // Pinned memory is accounted by the kernel by (real) uid across containers
    // (uid 1000 in the host and in containers will share the same count).
    // Sometimes root allowance (when no CAP_IPC_LOCK/CAP_SYS_ADMIN in a
    // container) is already exhausted, hence we switch to a different user.
    DDRES_CHECK_FWD(user_override_to_nobody_if_root(&info));

    if (!(region = perfown_sz(event->mapfd, event->ring_buffer_size))) {
      DDRES_RETURN_ERROR_LOG(
          DD_WHAT_PERFMMAP,
          "Could not mmap memory for watcher"
          "Please increase kernel limits on pinned memory (ulimit -l). "
          "OR associate the IPC_LOCK capability to this process.");
      }
  }

  if (!rb_init(&event->rb, region, event->ring_buffer_size,
               event->ring_buffer_type)) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFMMAP,
                             "Could not initialize ring buffer for watcher");
  }
  return {};
}


DDRes pevent_munmap_event(PEvent *event) {
  if (event->rb.base) {
    if (perfdisown(event->rb.base, event->ring_buffer_size) != 0) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFMMAP,
                             "Error when using perfdisown for watcher #%d",
                             event->watcher_pos);
    }
    event->rb.base = nullptr;
  }
  rb_free(&event->rb);
  return {};
}

/// Clean the mmap buffer
DDRes pevent_close_event(PEvent *event) {
  if (event->fd != -1) {
    if (close(event->fd) == -1) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                             "Error when closing fd=%d (watcher #%d) (%s)",
                             event->fd, event->watcher_pos, strerror(errno));
    }
    event->fd = -1;
  }
  if (event->custom_event && event->mapfd != -1) {
    if (close(event->mapfd) == -1) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                             "Error when closing mapfd=%d (watcher #%d) (%s)",
                             event->mapfd, event->watcher_pos, strerror(errno));
    }
  }
  return {};
}

} // namespace ddprof
