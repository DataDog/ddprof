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
DDRes pevent_create(PEventHdr *pevent_hdr, int watcher_idx,
                    size_t *pevent_idx) {
  if (pevent_hdr->size >= pevent_hdr->max_size) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Reached max number of watchers (%lu)",
                           pevent_hdr->max_size);
  }
  *pevent_idx = pevent_hdr->size++;
  pevent_hdr->pes[*pevent_idx].watcher_pos = watcher_idx;
  return {};
}

void display_system_config() {
  int val;
  DDRes const res = sys_perf_event_paranoid(val);
  if (IsDDResOK(res)) {
    LG_WRN("Check System Configuration - perf_event_paranoid=%d", val);
  } else {
    LG_WRN("Unable to access system configuration");
  }
}

// set info for a perf_event_open type of buffer
void pevent_set_info(int fd, int attr_idx, PEvent &pevent,
                     uint32_t stack_sample_size) {
  static bool log_once = true;
  pevent.fd = fd;
  pevent.mapfd = fd;
  int const buffer_size_order = pevent_compute_min_mmap_order(
      k_default_buffer_size_shift, stack_sample_size,
      k_min_number_samples_per_ring_buffer);
  if (buffer_size_order > k_default_buffer_size_shift && log_once) {
    LG_NTC("Increasing size order of the ring buffer to %d (from %d)",
           buffer_size_order, k_default_buffer_size_shift);
    log_once = false; // avoid flooding for all CPUs
  }
  pevent.ring_buffer_size = perf_mmap_size(buffer_size_order);
  pevent.custom_event = false;
  pevent.ring_buffer_type = RingBufferType::kPerfRingBuffer;
  pevent.attr_idx = attr_idx;
}

DDRes pevent_register_cpu_0(const PerfWatcher *watcher, int watcher_idx,
                            pid_t pid, PerfClockSource perf_clock_source,
                            PEventHdr *pevent_hdr, size_t &pevent_idx) {
  // register cpu 0 and find a working config
  PEvent *pes = pevent_hdr->pes;
  std::vector<perf_event_attr> perf_event_data =
      all_perf_configs_from_watcher(watcher, true, perf_clock_source);
  DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));

  // attempt with different configs
  for (auto &attr : perf_event_data) {
    // register cpu 0
    int const fd = perf_event_open(&attr, pid, 0, -1, PERF_FLAG_FD_CLOEXEC);
    if (fd != -1) {
      // Copy the successful config
      pevent_hdr->attrs[pevent_hdr->nb_attrs] = attr;
      pevent_set_info(fd, pevent_hdr->nb_attrs, pes[pevent_idx],
                      watcher->options.stack_sample_size);
      ++pevent_hdr->nb_attrs;
      assert(pevent_hdr->nb_attrs <= kMaxTypeWatcher);
      break;
    }
    LG_NFO("Expected failure (we retry with different settings) "
           "perf_event_open for watcher: %s - with attr.type=%s, "
           "exclude_kernel=%d",
           watcher->desc.c_str(), perf_type_str(attr.type),
           static_cast<int>(attr.exclude_kernel));
  }
  // check if one of the configs was successful
  if (pes[pevent_idx].attr_idx == -1) {
    display_system_config();
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Error calling perf_event_open on watcher %d.0 (%s)",
                           watcher_idx, strerror(errno));
  }

  return {};
}

DDRes pevent_open_all_cpus(const PerfWatcher *watcher, int watcher_idx,
                           std::span<pid_t> pids, int num_cpu,
                           PerfClockSource perf_clock_source,
                           PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;

  size_t template_pevent_idx = -1;
  DDRES_CHECK_FWD(pevent_register_cpu_0(watcher, watcher_idx, pids[0],
                                        perf_clock_source, pevent_hdr,
                                        template_pevent_idx));
  int const template_attr_idx = pes[template_pevent_idx].attr_idx;
  perf_event_attr *attr = &pevent_hdr->attrs[template_attr_idx];

  // used the fixed attr for the others
  for (int cpu_idx = 0; cpu_idx < num_cpu; ++cpu_idx) {
    PEvent *pevent = nullptr;
    // skip first cpu, since already done in pevent_register_cpu_0
    if (cpu_idx > 0) {
      size_t pevent_idx = -1;
      DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));
      int const fd =
          perf_event_open(attr, pids[0], cpu_idx, -1, PERF_FLAG_FD_CLOEXEC);
      if (fd == -1) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                               "Error calling perfopen on watcher %d.%d (%s)",
                               watcher_idx, cpu_idx, strerror(errno));
      }
      pevent_set_info(fd, pes[template_pevent_idx].attr_idx, pes[pevent_idx],
                      watcher->options.stack_sample_size);
      pevent = &pes[pevent_idx];
    } else {
      pevent = &pes[template_pevent_idx];
    }
    // do perf_event_open for the other tids, but record them as sub fds
    // attached to the first. These are not mmaped, but their output will
    // be redirected to the first one.
    for (auto tid : pids.subspan(1)) {
      int const fd =
          perf_event_open(attr, tid, cpu_idx, -1, PERF_FLAG_FD_CLOEXEC);
      if (fd == -1) {
        // Ignore failure, thread may have exited
        LG_WRN("Error calling perf_event_open on watcher %d.%d (%s) for tid %d",
               watcher_idx, cpu_idx, strerror(errno), tid);
      } else {
        pevent->sub_fds.push_back(fd);
      }
    }
  }
  return {};
}

/// Setup perf event according to requested watchers.
DDRes pevent_open(DDProfContext &ctx, std::span<pid_t> pids, int num_cpu,
                  PEventHdr *pevent_hdr) {
  assert(pevent_hdr->size == 0); // check for previous init
  for (unsigned long watcher_idx = 0; watcher_idx < ctx.watchers.size();
       ++watcher_idx) {
    PerfWatcher *watcher = &ctx.watchers[watcher_idx];
    if (watcher->type < kDDPROF_TYPE_CUSTOM) {
      DDRES_CHECK_FWD(pevent_open_all_cpus(watcher, watcher_idx, pids, num_cpu,
                                           ctx.perf_clock_source, pevent_hdr));
    } else {
      // custom event, eg.allocation profiling
      size_t pevent_idx = 0;
      DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));
      int const order = pevent_compute_min_mmap_order(
          k_mpsc_buffer_size_shift, watcher->options.stack_sample_size,
          k_min_number_samples_per_ring_buffer);
      DDRES_CHECK_FWD(ring_buffer_create(order, RingBufferType::kMPSCRingBuffer,
                                         true, &pevent_hdr->pes[pevent_idx]));
    }
  }
  return {};
}

} // namespace

void pevent_init(PEventHdr *pevent_hdr) {
  *pevent_hdr = {};
  pevent_hdr->max_size = k_max_nb_perf_event_open;
  for (size_t k = 0; k < pevent_hdr->max_size; ++k) {
    pevent_hdr->pes[k].fd = -1;
    pevent_hdr->pes[k].mapfd = -1;
    pevent_hdr->pes[k].attr_idx = -1;
  }
}

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
  if (event->mapfd != -1) {
    void *region = perfown_sz(event->mapfd, event->ring_buffer_size);
    if (!region) {
      DDRES_RETURN_ERROR_LOG(
          DD_WHAT_PERFMMAP,
          "Could not mmap memory for watcher #%d: %s. "
          "Please increase kernel limits on pinned memory (ulimit -l). "
          "OR associate the IPC_LOCK capability to this process.",
          event->watcher_pos, strerror(errno));
    }
    if (!rb_init(&event->rb, region, event->ring_buffer_size,
                 event->ring_buffer_type)) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFMMAP,
                             "Could not initialize ring buffer for watcher #%d",
                             event->watcher_pos);
    }
  }
  return {};
}

DDRes pevent_mmap(PEventHdr *pevent_hdr, bool use_override) {
  // Switch user if needed (when root switch to nobody user)
  // Pinned memory is accounted by the kernel by (real) uid across containers
  // (uid 1000 in the host and in containers will share the same count).
  // Sometimes root allowance (when no CAP_IPC_LOCK/CAP_SYS_ADMIN in a
  // container) is already exhausted, hence we switch to a different user.
  UIDInfo info;
  if (use_override) {
    /* perf_event_mlock_kb is accounted per real user id */
    DDRES_CHECK_FWD(user_override_to_nobody_if_root(&info));
  }

  defer {
    if (use_override) {
      user_override(info.uid, info.gid);
    }
  };

  auto defer_munmap = make_defer([&] { pevent_munmap(pevent_hdr); });

  PEvent *pes = pevent_hdr->pes;
  for (size_t k = 0; k < pevent_hdr->size; ++k) {
    DDRES_CHECK_FWD(pevent_mmap_event(&pes[k]));
  }

  defer_munmap.release();

  return {};
}

DDRes pevent_setup(DDProfContext &ctx, std::span<pid_t> pids, int num_cpu,
                   PEventHdr *pevent_hdr) {
  DDRES_CHECK_FWD(pevent_open(ctx, pids, num_cpu, pevent_hdr));
  if (!IsDDResOK(pevent_mmap(pevent_hdr, true))) {
    LG_NTC("Retrying attachment without user override");
    DDRES_CHECK_FWD(pevent_mmap(pevent_hdr, false));
  }

  return {};
}

DDRes pevent_enable(PEventHdr *pevent_hdr) {
  // Just before we enter the main loop, force the enablement of the perf
  // contexts
  for (size_t i = 0; i < pevent_hdr->size; ++i) {
    if (!pevent_hdr->pes[i].custom_event) {
      for (auto fd : pevent_hdr->pes[i].sub_fds) {
        // Redirect the output of the sub fds to the main one
        DDRES_CHECK_INT(
            ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, pevent_hdr->pes[i].fd),
            DD_WHAT_IOCTL,
            "Error ioctl PERF_EVENT_IOC_SET_OUTPUT fd=%d output_fd=%d "
            "(idx#%zu)",
            fd, pevent_hdr->pes[i].fd, i);
        DDRES_CHECK_INT(ioctl(fd, PERF_EVENT_IOC_ENABLE), DD_WHAT_IOCTL,
                        "Error ioctl fd=%d (idx#%zu)", fd, i);
      }

      DDRES_CHECK_INT(ioctl(pevent_hdr->pes[i].fd, PERF_EVENT_IOC_ENABLE),
                      DD_WHAT_IOCTL, "Error ioctl fd=%d (idx#%zu)",
                      pevent_hdr->pes[i].fd, i);
    }
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
DDRes pevent_munmap(PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;
  DDRes res{};

  for (size_t k = 0; k < pevent_hdr->size; ++k) {
    DDRes const local_res = pevent_munmap_event(&pes[k]);
    if (!IsDDResOK(local_res)) {
      res = local_res;
    }
  }

  return res;
}

DDRes pevent_close_event(PEvent *event) {
  if (event->fd != -1) {
    if (close(event->fd) == -1) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                             "Error when closing fd=%d (watcher #%d) (%s)",
                             event->fd, event->watcher_pos, strerror(errno));
    }
    event->fd = -1;
    for (auto sub_fd : event->sub_fds) {
      if (close(sub_fd) == -1) {
        DDRES_RETURN_ERROR_LOG(
            DD_WHAT_PERFOPEN, "Error when closing sub_fd=%d (watcher #%d) (%s)",
            sub_fd, event->watcher_pos, strerror(errno));
      }
    }
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

DDRes pevent_close(PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;
  DDRes res{};
  for (size_t k = 0; k < pevent_hdr->size; ++k) {
    DDRes const local_res = pevent_close_event(&pes[k]);
    if (!IsDDResOK(local_res)) {
      res = local_res;
    }
  }
  pevent_hdr->size = 0;
  return res;
}

bool pevent_include_kernel_events(const PEventHdr *pevent_hdr) {
  for (size_t i = 0; i < pevent_hdr->nb_attrs; ++i) {
    if (pevent_hdr->attrs[i].exclude_kernel == 0) {
      return true;
    }
  }
  return false;
}

DDRes pevent_cleanup(PEventHdr *pevent_hdr) {
  DDRes ret = ddres_init();

  // Cleanup both, storing the error if one was generated
  if (DDRes const ret_tmp = pevent_munmap(pevent_hdr); !IsDDResOK((ret_tmp))) {
    ret = ret_tmp;
  }
  if (DDRes const ret_tmp = pevent_close(pevent_hdr); !IsDDResOK((ret_tmp))) {
    ret = ret_tmp;
  }
  return ret;
}
} // namespace ddprof
