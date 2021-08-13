#pragma once

#include "ddprof_context.h"
#include "perf.h"
#include "pevent.h"

void main_loop(PEventHdr *pevent_hdr, perfopen_attr *, DDProfContext *);
