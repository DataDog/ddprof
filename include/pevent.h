// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <sys/types.h>

#include "perf_ringbuffer.h"

#define MAX_NB_WATCHERS 450

typedef struct PEvent {
  int pos;       // Index into the sample
  int fd;        // Underlying perf event FD
  RingBuffer rb; // metadata and buffers for processing perf ringbuffer
} PEvent;

typedef struct PEventHdr {
  PEvent pes[MAX_NB_WATCHERS];
  size_t size;
  size_t max_size;
} PEventHdr;
