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
#include "sys_utils.hpp"
#include "syscalls.hpp"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/eventfd.h>
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
  for (size_t k = 0; k < pevent_hdr->max_size; ++k)
    pevent_hdr->pes[k].fd = -1;
}

DDRes pevent_create_custom_ring_buffer(PEvent *pevent,
                                       size_t ring_buffer_size_order) {
  pevent->mapfd =
      ddprof::memfd_create("allocation_ring_buffer", 1U /*MFD_CLOEXEC*/);
  if (pevent->mapfd == -1) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Error calling memfd_create on watcher %d (%s)",
                           pevent->pos, strerror(errno));
  }
  if (ftruncate(pevent->mapfd, perf_mmap_size(ring_buffer_size_order)) == -1) {
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
  return {};
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
        pes[pevent_idx].custom_event = false;
      }
    } else {
      // custom event, eg.allocation profiling
      size_t pevent_idx = 0;
      DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));
      DDRES_CHECK_FWD(pevent_create_custom_ring_buffer(
          &pevent_hdr->pes[pevent_idx], DEFAULT_BUFF_SIZE_SHIFT));
    }
  }
  return ddres_init();
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
    if (pes[k].mapfd != -1) {
      size_t reg_sz = 0;
      // Do not mirror perf ring buffer because this doubles the amount of
      // mlocked pages
      bool mirror = pes[k].custom_event;

      perf_event_mmap_page *region = static_cast<perf_event_mmap_page *>(
          perfown(pes[k].mapfd, mirror, &reg_sz));
      if (!region) {
        DDRES_RETURN_ERROR_LOG(
            DD_WHAT_PERFMMAP,
            "Could not finalize watcher (idx#%zu): registration (%s)", k,
            strerror(errno));
      }
      if (!rb_init(&pes[k].rb, region, reg_sz, mirror)) {
        DDRES_RETURN_ERROR_LOG(
            DD_WHAT_PERFMMAP,
            "Could not allocate storage for watcher (idx#%zu)", k);
      }
    }
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

/// Clean the mmap buffer
DDRes pevent_munmap(PEventHdr *pevent_hdr) {

  PEvent *pes = pevent_hdr->pes;
  for (size_t k = 0; k < pevent_hdr->size; ++k) {
    if (pes[k].rb.region) {
      if (perfdisown(pes[k].rb.region, pes[k].rb.size, pes[k].rb.is_mirrored) !=
          0) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFMMAP,
                               "Error when using perfdisown %zu", k);
      }
      pes[k].rb.region = NULL;
    }
    rb_free(&pevent_hdr->pes[k].rb);
  }

  return ddres_init();
}

DDRes pevent_close(PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;
  for (size_t k = 0; k < pevent_hdr->size; ++k) {
    if (pes[k].fd != -1) {
      if (close(pes[k].fd) == -1) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                               "Error when closing fd=%d (idx#%zu) (%s)",
                               pes[k].fd, k, strerror(errno));
      }
      pes[k].fd = -1;
    }
    if (pes[k].custom_event && pes[k].mapfd != -1) {
      if (close(pes[k].mapfd) == -1) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                               "Error when closing mapfd=%d (idx#%zu) (%s)",
                               pes[k].mapfd, k, strerror(errno));
      }
    }
  }
  pevent_hdr->size = 0;
  return ddres_init();
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
