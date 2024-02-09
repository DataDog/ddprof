// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <cstdint>

namespace ddprof {

enum class DsoType : uint8_t {
  kStandard = 0, // meaning backed by a file that we can open
  kVdso,
  kVsysCall,
  kStack,
  kHeap,
  kUndef,
  kAnon,
  kRuntime,
  kSocket,
  kDDProfiling, // special case in which the library might be known internally
  kJITDump,     // jit dump file (LLVM guarantee they mmap this as a marker)
  kNbDsoTypes
};

inline bool has_relevant_path(DsoType dso_type) {
  if (dso_type == DsoType::kDDProfiling) {
    return true;
  }
  if (dso_type == DsoType::kStandard) {
    return true;
  }
  return false;
}

// todo : find an enum that supports to_str
inline const char *dso_type_str(DsoType path_type) {
  switch (path_type) {
  case DsoType::kStandard:
    return "Standard";
  case DsoType::kVdso:
    return "Vdso";
  case DsoType::kVsysCall:
    return "VsysCall";
  case DsoType::kStack:
    return "Stack";
  case DsoType::kHeap:
    return "Heap";
  case DsoType::kUndef:
    return "Undefined";
  case DsoType::kAnon:
    return "Anonymous";
  case DsoType::kRuntime:
    return "Runtime";
  case DsoType::kSocket:
    return "kSocket";
  case DsoType::kDDProfiling:
    return "kDDProfiling";
  default:
    break;
  }
  return "Unhandled";
}

} // namespace ddprof
