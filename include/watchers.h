#pragma once

#include "ddprof_context.h"
#include "ddres_def.h"
#include "pevent.h"

#warning move this
void init_pevent(PEventHdr *pevent_hdr);

/// Setup watchers according to ddprof context, return nb watchers setup
DDRes setup_watchers(DDProfContext *ctx, pid_t pid, int num_cpu,
                     PEventHdr *pevent_hdr);

/// Call ioctl PERF_EVENT_IOC_ENABLE on available file descriptors
DDRes enable_watchers(PEventHdr *pevent_hdr);

/// cleanup watchers according to ddprof context, return nb watchers cleaned
DDRes cleanup_watchers(PEventHdr *pevent_hdr);
