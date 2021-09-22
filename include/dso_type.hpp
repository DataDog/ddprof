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
  kNbDsoTypes
};

// todo : find an enum that supports to_str
static inline const char *dso_type_str(DsoType path_type) {
  switch (path_type) {
  case kStandard:
    return "kStandard";
  case kVdso:
    return "kVdso";
  case kVsysCall:
    return "kVsysCall";
  case kStack:
    return "kStack";
  case kHeap:
    return "kHeap";
  case kUndef:
    return "kUndef";
  default:
    return "unhandled";
  }
  return "unhandled";
}
} // namespace dso

} // namespace ddprof