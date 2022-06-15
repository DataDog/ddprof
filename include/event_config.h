// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum EventConfType {
  EVENT_NONE = 0, // note:  this is not allowed, but named for consistency
  EVENT_CALLGRAPH = 1 << 0,
  EVENT_METRIC = 1 << 1,
  EVENT_BOTH = EVENT_CALLGRAPH | EVENT_METRIC,
} EventConfType;

typedef enum EventConfLocationType {
  ECLOC_FREQ = 0, // count simple events
  ECLOC_REG = 1,  // Use the register specified in `register_num`
  ECLOC_RAW = 2,  // Use the offset/size into the underlying RAW event
} EventConfLocationType;

typedef enum EventConfCadenceType {
  ECCAD_UNDEF = 0,
  ECCAD_PERIOD = 1,
  ECCAD_FREQ = 2,
} EventConfCadenceType;

typedef struct EventConf {
  EventConfType type;
  bool has_id;
  uint64_t id;
  const char *eventname;
  const char *groupname;
  EventConfLocationType loc_type;
  uint8_t register_num;
  uint8_t size;
  uint64_t offset;
  EventConfCadenceType cad_type;
  uint64_t cadence;
} EventConf;
