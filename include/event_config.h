// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum EventConfMode{
  EVENT_NONE      = 0,
  EVENT_CALLGRAPH = 1 << 0,
  EVENT_METRIC    = 1 << 1,
  EVENT_BOTH      = EVENT_CALLGRAPH | EVENT_METRIC,
} EventConfMode;

typedef enum EventConfLocationType {
  ECLOC_VAL = 0, // Use sample value from perf events
  ECLOC_REG = 1, // Use the register from `register_num`
  ECLOC_RAW = 2, // Use the offset/size for raw event
} EventConfLocationType;

typedef enum EventConfCadenceType {
  ECCAD_UNDEF = 0,
  ECCAD_PERIOD = 1,
  ECCAD_FREQ = 2,
} EventConfCadenceType;

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
  EventConfField field;  // For parsing
  EventConfMode mode;

  uint64_t id;

  char *label;
  char *eventname;
  char *groupname;

  EventConfLocationType loc_type;
  uint8_t register_num;
  uint8_t arg_size;
  uint64_t arg_offset;
  double arg_coeff;

  EventConfCadenceType cad_type;
  uint64_t cadence;
} EventConf;
