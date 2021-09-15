#pragma once

#include "ddprof_context.h"
#include "worker_attr.h"

/**
 * Continuously poll for new events and process them accordingly
 *
 * The main loop handles forking to contain potential memory growth.
 *
 * @param pevent_hdr objects to manage incoming events and api with
 * perf_event_opem
 * @param WorkerAttr set of functions to customize worker behaviour
 *
 * @return
 */
void main_loop(const WorkerAttr *, DDProfContext *);

// Same as main loop without any forks
void main_loop_lib(const WorkerAttr *attr, DDProfContext *ctx);
