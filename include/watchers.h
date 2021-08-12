#pragma once

#include "ddprof_context.h"
#include "ddres_def.h"
#include "pevent.h"

#warning move this
void init_pevent(PEventHdr *pevent_hdr);

/// Setup perf event according to requested watchers.
DDRes setup_perfevent(DDProfContext *ctx, pid_t pid, int num_cpu,
                      PEventHdr *pevent_hdr);

/// Setup mmap buffers according to content of peventhdr
DDRes setup_mmap(PEventHdr *pevent_hdr);

/// Setup watchers = setup mmap + setup perfevent
DDRes setup_watchers(DDProfContext *ctx, pid_t pid, int num_cpu,
                     PEventHdr *pevent_hdr);

/// Call ioctl PERF_EVENT_IOC_ENABLE on available file descriptors
DDRes enable_watchers(PEventHdr *pevent_hdr);

/// Clean the buffers allocated by mmap
DDRes cleanup_mmap(PEventHdr *pevent_hdr);

/// Clean the file descriptors
DDRes cleanup_perfevent(PEventHdr *pevent_hdr);

/// cleanup watchers = cleanup perfevent + cleanup mmap (clean everything)
DDRes cleanup_watchers(PEventHdr *pevent_hdr);
