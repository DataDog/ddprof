#pragma once

#include "ddprof_context.h"
#include "perf.h"
#include "pevent.h"

/**
 * Continuously poll for new events and process them accordingly
 *
 * The main loop handles forking to contain potential memory growth.
 *
 * @param pevent_hdr objects to manage incoming events and api with
 * perf_event_opem
 * @param perfopen_attr set of functions to manage new events / timeouts
 *
 * @return
 */
void main_loop(PEventHdr *pevent_hdr, perfopen_attr *, DDProfContext *);
