#pragma once

#include "ddprof_context.h"
#include "ddres_def.h"
#include "pevent.h"

/// Sets initial state for every pevent in the pevent_hdr
void pevent_init(PEventHdr *pevent_hdr);

/// Setup perf event according to requested watchers.
DDRes pevent_open(DDProfContext *ctx, pid_t pid, int num_cpu,
                  PEventHdr *pevent_hdr);

/// Setup mmap buffers according to content of peventhdr
DDRes pevent_mmap(PEventHdr *pevent_hdr, bool use_override);

/// Setup watchers = setup mmap + setup perfevent
DDRes pevent_setup(DDProfContext *ctx, pid_t pid, int num_cpu,
                   PEventHdr *pevent_hdr);

/// Call ioctl PERF_EVENT_IOC_ENABLE on available file descriptors
DDRes pevent_enable(PEventHdr *pevent_hdr);

/// Clean the buffers allocated by mmap
DDRes pevent_munmap(PEventHdr *pevent_hdr);

/// Clean the file descriptors
DDRes pevent_close(PEventHdr *pevent_hdr);

/// cleanup watchers = cleanup perfevent + cleanup mmap (clean everything)
DDRes pevent_cleanup(PEventHdr *pevent_hdr);
