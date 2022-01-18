// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

namespace ddprof {
namespace dso {
enum DsoType {
  kStandard = 0, // meaning backed by a file that we can open
  kVdso,
  kVsysCall,
  kStack,
  kHeap,
  kUndef,
  kAnon,
  kSocket,
  kNbDsoTypes
};

// todo : find an enum that supports to_str
static inline const char *dso_type_str(DsoType path_type) {
  switch (path_type) {
  case kStandard:
    return "Standard";
  case kVdso:
    return "Vdso";
  case kVsysCall:
    return "VsysCall";
  case kStack:
    return "Stack";
  case kHeap:
    return "Heap";
  case kUndef:
    return "Undefined";
  case kAnon:
    return "Anonymous";
  case kSocket:
    return "kSocket";
  default:
    break;
  }
  return "Unhandled";
}
} // namespace dso

} // namespace ddprof
