// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

extern "C" {
#include "pevent_lib.h"

#include "ddres.h"
#include "perf.h"
#include "user_override.h"
}

#include "defer.hpp"
#include "perf.hpp"
#include "ringbuffer_utils.hpp"
#include "sys_utils.hpp"
#include "syscalls.hpp"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

struct PerfEventAttributes {
  std::vector<perf_event_attr> _data;
};

static DDRes pevent_create(PEventHdr *pevent_hdr, int watcher_idx,
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

void pevent_init(PEventHdr *pevent_hdr) {
  memset(pevent_hdr, 0, sizeof(PEventHdr));
  pevent_hdr->max_size = MAX_NB_PERF_EVENT_OPEN;
  for (size_t k = 0; k < pevent_hdr->max_size; ++k) {
    pevent_hdr->pes[k].fd = -1;
    pevent_hdr->pes[k].mapfd = -1;
    pevent_hdr->pes[k].attr_idx = -1;
  }
  pevent_hdr->attrs = new PerfEventAttributes();
}

static void display_system_config(void) {
  int val;
  DDRes res = ddprof::sys_perf_event_paranoid(val);
  if (IsDDResOK(res)) {
    LG_WRN("Check System Configuration - perf_event_paranoid=%d", val);
  } else {
    LG_WRN("Unable to access system configuration");
  }
}

// set info for a perf_event_open type of buffer
static void pevent_set_info(int fd, int attr_idx, PEvent &pevent) {
  pevent.fd = fd;
  pevent.mapfd = fd;
  pevent.ring_buffer_size = perf_mmap_size(DEFAULT_BUFF_SIZE_SHIFT);
  pevent.custom_event = false;
  pevent.attr_idx = attr_idx;
}

static DDRes pevent_register_cpu_0(const PerfWatcher *watcher, int watcher_idx,
                                   pid_t pid, PEventHdr *pevent_hdr,
                                   size_t &pevent_idx) {
  // register cpu 0 and find a working config
  PEvent *pes = pevent_hdr->pes;
  std::vector<perf_event_attr> perf_event_data =
      ddprof::all_perf_configs_from_watcher(watcher, true);
  DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));

  // attempt with different configs
  for (auto &attr : perf_event_data) {
    // register cpu 0
    int fd = perf_event_open(&attr, pid, 0, -1, PERF_FLAG_FD_CLOEXEC);
    if (fd != -1) {
      // Copy the successful config
      pevent_hdr->attrs->_data.push_back(attr);
      pevent_set_info(fd, pevent_hdr->attrs->_data.size() - 1, pes[pevent_idx]);
      break;
    }
  }
  // check if one of the configs was successful
  if (pes[pevent_idx].attr_idx == -1) {
    display_system_config();
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Error calling perfopen on watcher %d.0 (%s)",
                           watcher_idx, strerror(errno));
  }

  return ddres_init();
}

static DDRes pevent_open_all_cpus(const PerfWatcher *watcher, int watcher_idx,
                                  pid_t pid, int num_cpu,
                                  PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;

  size_t template_pevent_idx = -1;
  DDRES_CHECK_FWD(pevent_register_cpu_0(watcher, watcher_idx, pid, pevent_hdr,
                                        template_pevent_idx));
  int template_attr_idx = pes[template_pevent_idx].attr_idx;
  perf_event_attr *attr = &pevent_hdr->attrs->_data[template_attr_idx];

  // used the fixed attr for the others
  for (int cpu_idx = 1; cpu_idx < num_cpu; ++cpu_idx) {
    size_t pevent_idx = -1;
    DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));
    int fd = perf_event_open(attr, pid, cpu_idx, -1, PERF_FLAG_FD_CLOEXEC);
    if (fd == -1) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                             "Error calling perfopen on watcher %d.%d (%s)",
                             watcher_idx, cpu_idx, strerror(errno));
    }
    pevent_set_info(fd, pes[template_pevent_idx].attr_idx, pes[pevent_idx]);
  }
  return ddres_init();
}

DDRes pevent_open(DDProfContext *ctx, pid_t pid, int num_cpu,
                  PEventHdr *pevent_hdr) {
  assert(pevent_hdr->size == 0); // check for previous init
  for (int watcher_idx = 0; watcher_idx < ctx->num_watchers; ++watcher_idx) {
    if (ctx->watchers[watcher_idx].type < kDDPROF_TYPE_CUSTOM) {
      DDRES_CHECK_FWD(pevent_open_all_cpus(
          &ctx->watchers[watcher_idx], watcher_idx, pid, num_cpu, pevent_hdr));
    } else {
      // custom event, eg.allocation profiling
      size_t pevent_idx = 0;
      DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));
      DDRES_CHECK_FWD(ddprof::ring_buffer_create(DEFAULT_BUFF_SIZE_SHIFT,
                                                 &pevent_hdr->pes[pevent_idx]));
    }
  }
  return ddres_init();
}

DDRes pevent_mmap_event(PEvent *event) {
  if (event->mapfd != -1) {
    // Do not mirror perf ring buffer because this doubles the amount of
    // mlocked pages
    bool mirror = event->custom_event;

    perf_event_mmap_page *region = static_cast<perf_event_mmap_page *>(
        perfown_sz(event->mapfd, event->ring_buffer_size, mirror));
    if (!region) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFMMAP,
                             "Could not mmap memory for watcher #%d: %s",
                             event->watcher_pos, strerror(errno));
    }
    if (!rb_init(&event->rb, region, event->ring_buffer_size, mirror)) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFMMAP,
                             "Could not allocate memory for watcher #%d",
                             event->watcher_pos);
    }
  }
  return {};
}

DDRes pevent_mmap(PEventHdr *pevent_hdr, bool use_override) {
  // Switch user if needed (when root switch to nobody user)
  // pinned memory accounting in root user does not always work
  // hence we use a different user
  UIDInfo info;
  if (use_override) {
    DDRES_CHECK_FWD(user_override(&info));
  }

  defer {
    if (use_override)
      revert_override(&info);
  };

  auto defer_munmap = make_defer([&] { pevent_munmap(pevent_hdr); });

  PEvent *pes = pevent_hdr->pes;
  for (size_t k = 0; k < pevent_hdr->size; ++k) {
    DDRES_CHECK_FWD(pevent_mmap_event(&pes[k]));
  }

  defer_munmap.release();

  return {};
}

DDRes pevent_setup(DDProfContext *ctx, pid_t pid, int num_cpu,
                   PEventHdr *pevent_hdr) {
  DDRES_CHECK_FWD(pevent_open(ctx, pid, num_cpu, pevent_hdr));
  DDRES_CHECK_FWD(pevent_mmap(pevent_hdr, true));
  return ddres_init();
}

DDRes pevent_enable(PEventHdr *pevent_hdr) {
  // Just before we enter the main loop, force the enablement of the perf
  // contexts
  for (size_t i = 0; i < pevent_hdr->size; ++i) {
    if (!pevent_hdr->pes[i].custom_event) {
      DDRES_CHECK_INT(ioctl(pevent_hdr->pes[i].fd, PERF_EVENT_IOC_ENABLE),
                      DD_WHAT_IOCTL, "Error ioctl fd=%d (idx#%zu)",
                      pevent_hdr->pes[i].fd, i);
    }
  }
  return ddres_init();
}

DDRes pevent_munmap_event(PEvent *event) {
  if (event->rb.region) {
    if (perfdisown(event->rb.region, event->rb.size, event->rb.is_mirrored) !=
        0) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFMMAP,
                             "Error when using perfdisown for watcher #%d",
                             event->watcher_pos);
    }
    event->rb.region = NULL;
  }
  rb_free(&event->rb);
  return {};
}

/// Clean the mmap buffer
DDRes pevent_munmap(PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;
  DDRes res{};

  for (size_t k = 0; k < pevent_hdr->size; ++k) {
    DDRes local_res = pevent_munmap_event(&pes[k]);
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
    DDRes local_res = pevent_close_event(&pes[k]);
    if (!IsDDResOK(local_res)) {
      res = local_res;
    }
  }
  pevent_hdr->size = 0;
  delete pevent_hdr->attrs;
  pevent_hdr->attrs = nullptr;
  return res;
}

bool pevent_include_kernel_events(const PEventHdr *pevent_hdr) {
  for (const auto &attr : pevent_hdr->attrs->_data) {
    if (attr.exclude_kernel == 0) {
      return true;
    }
  }
  return false;
}

DDRes pevent_cleanup(PEventHdr *pevent_hdr) {
  DDRes ret = ddres_init();
  DDRes ret_tmp;

  // Cleanup both, storing the error if one was generated
  if (!IsDDResOK(ret_tmp = pevent_munmap(pevent_hdr)))
    ret = ret_tmp;
  if (!IsDDResOK(ret_tmp = pevent_close(pevent_hdr)))
    ret = ret_tmp;
  return ret;
}
