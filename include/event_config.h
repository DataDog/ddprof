// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Defines how a sample is aggregated when it is received
typedef enum EventConfMode {
  EVENT_NONE = 0,
  EVENT_CALLGRAPH = 1 << 0,
  EVENT_METRIC = 1 << 1,
  EVENT_BOTH = EVENT_CALLGRAPH | EVENT_METRIC,
} EventConfMode;

// Defines how samples are weighted
typedef enum EventConfLocationType {
  ECLOC_VAL = 0, // Use sample value from perf events
  ECLOC_REG = 1, // Use the register from `register_num`
  ECLOC_RAW = 2, // Use the offset/size for raw event
} EventConfLocationType;

// Defines how the sampling is configured (e.g., with `perf_event_open()`)
typedef enum EventConfCadenceType {
  ECCAD_UNDEF = 0,
  ECCAD_PERIOD = 1,
  ECCAD_FREQ = 2,
} EventConfCadenceType;

// Used by the parser to return which key was detected
typedef enum EventConfField {
  ECF_NONE,
  ECF_ARGCOEFF,
  ECF_ARGOFFSET,
  ECF_ARGSIZE,
  ECF_EVENT,
  ECF_FREQUENCY,
  ECF_GROUP,
  ECF_ID,
  ECF_LABEL,
  ECF_LOCATION,
  ECF_MODE,
  ECF_PARAMETER,
  ECF_PERIOD,
  ECF_REGISTER,
} EventConfField;

typedef struct EventConf {
  EventConfMode mode;

  uint64_t id;

  char *eventname;
  char *groupname;
  char *label;

  EventConfLocationType loc_type;
  uint8_t register_num;
  uint8_t arg_size;
  uint64_t arg_offset;
  double arg_coeff;

  EventConfCadenceType cad_type;
  uint64_t cadence;
} EventConf;

// This header will be used both by the ddprof C++ code and the C compiler for
// the parser-generator
#ifdef __cplusplus
extern "C" {
#endif
EventConf *EventConf_parse(const char *msg); // provided by event_parser.c
#ifdef __cplusplus
}
#endif