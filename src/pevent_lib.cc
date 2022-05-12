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

static DDRes pevent_create(PEventHdr *pevent_hdr, int watcher_idx,
                           size_t *pevent_idx) {
  if (pevent_hdr->size >= pevent_hdr->max_size) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Reached max number of watchers (%lu)",
                           pevent_hdr->max_size);
  }
  *pevent_idx = pevent_hdr->size++;
  pevent_hdr->pes[*pevent_idx].pos = watcher_idx;
  return {};
}

void pevent_init(PEventHdr *pevent_hdr) {
  memset(pevent_hdr, 0, sizeof(PEventHdr));
  pevent_hdr->max_size = MAX_NB_WATCHERS;
  for (size_t k = 0; k < pevent_hdr->max_size; ++k) {
    pevent_hdr->pes[k].fd = -1;
    pevent_hdr->pes[k].mapfd = -1;
  }
}

static void display_system_config(void) {
  int val;
  DDRes res = ddprof::sys_perf_event_paranoid(val);
  if (IsDDResOK(res)) {
    LG_NFO("Check System Configuration - perf_event_paranoid=%d", val);
  } else {
    LG_NFO("Unable to access system configuration");
  }
}

DDRes pevent_open(DDProfContext *ctx, pid_t pid, int num_cpu,
                  PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;
  assert(pevent_hdr->size == 0); // check for previous init
  for (int watcher_idx = 0; watcher_idx < ctx->num_watchers; ++watcher_idx) {
    if (ctx->watchers[watcher_idx].type < kDDPROF_TYPE_CUSTOM) {
      for (int cpu_idx = 0; cpu_idx < num_cpu; ++cpu_idx) {
        size_t pevent_idx = 0;
        DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));

        pes[pevent_idx].fd =
            perfopen(pid, &ctx->watchers[watcher_idx], cpu_idx, true);
        if (pes[pevent_idx].fd == -1) {
          display_system_config();
          DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                                 "Error calling perfopen on watcher %d.%d (%s)",
                                 watcher_idx, cpu_idx, strerror(errno));
        }
        pes[pevent_idx].mapfd = pes[pevent_idx].fd;
        pevent_hdr->pes[pevent_idx].ring_buffer_size =
            perf_mmap_size(DEFAULT_BUFF_SIZE_SHIFT);
        pes[pevent_idx].custom_event = false;
      }
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
                             event->pos, strerror(errno));
    }
    if (!rb_init(&event->rb, region, event->ring_buffer_size, mirror)) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFMMAP,
                             "Could not allocate memory for watcher #%d",
                             event->pos);
    }
  }
  return {};
}

DDRes pevent_mmap(PEventHdr *pevent_hdr, bool use_override) {
  // Switch user if needed (when root switch to nobody user)
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
                             event->pos);
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
                             event->fd, event->pos, strerror(errno));
    }
    event->fd = -1;
  }
  if (event->custom_event && event->mapfd != -1) {
    if (close(event->mapfd) == -1) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                             "Error when closing mapfd=%d (watcher #%d) (%s)",
                             event->mapfd, event->pos, strerror(errno));
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
  return res;
}

// returns the number of successful cleans
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
