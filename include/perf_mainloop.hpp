// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_context.hpp"
#include "worker_attr.hpp"

namespace ddprof {
/**
 * Continuously poll for new events and process them accordingly
 *
 * The main loop handles forking to contain potential memory growth.
 *
 * @param pevent_hdr objects to manage incoming events and api with
 * perf_event_open
 * @param WorkerAttr set of functions to customize worker behaviour
 *
 * @return
 */
DDRes main_loop(const WorkerAttr *attr, DDProfContext *ctx);

} // namespace ddprof
