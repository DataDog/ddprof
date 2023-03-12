// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stdint.h>
#include <string>

// Defines how a sample is aggregated when it is received
enum class EventConfMode : uint32_t {
  kDisabled = 0,
  kCallgraph = 1 << 0,     // flamegraph of resource usage
  kMetric = 1 << 1,        // gauge of resource usage
  kLiveCallgraph = 1 << 2, // report callgraph of resources still in use
  kAll = kCallgraph | kMetric | kLiveCallgraph,
};

constexpr EventConfMode operator|(EventConfMode A, const EventConfMode B) {
  return static_cast<EventConfMode>(static_cast<uint32_t>(A) |
                                    static_cast<uint32_t>(B));
}

constexpr EventConfMode operator|=(EventConfMode &A, const EventConfMode B) {
  return A = A | B;
}

constexpr EventConfMode operator&(const EventConfMode A,
                                  const EventConfMode B) {
  return static_cast<EventConfMode>(static_cast<uint32_t>(A) &
                                    static_cast<uint32_t>(B));
}

constexpr bool Any(EventConfMode arg) {
  return arg != EventConfMode::kDisabled;
}

constexpr bool AnyCallgraph(EventConfMode arg) {
  return Any((arg & EventConfMode::kLiveCallgraph) |
             (arg & EventConfMode::kCallgraph));
}

// Defines how samples are weighted
enum class EventConfValueSource {
  kSample = 0,   // Use sample value (period) from perf events
  kRegister = 1, // Use the register from `register_num`
  kRaw = 2,      // Use the offset/size for raw event
};

// Defines how the sampling is configured (e.g., with `perf_event_open()`)
enum class EventConfCadenceType {
  kUndefined = 0,
  kPeriod = 1,
  kFrequency = 2,
};

// Used by the parser to return which key was detected
enum class EventConfField {
  kNone = 0,
  /*
   *  None is an invalid event type used to fence uninitialized values.
   */
  kValueScale,
  /*
   *  ValueScale defines a real-valued coefficient which is used to scale the
   *  sample value when the correspond watcher is retrieved.  This is useful
   *  because multiple tracepoints may be globbed together in the Profiling
   *  UX and individual watchers therein may need to be scaled differently
   *  (e.g., mmap and munmap together although this is a bad example).
   */
  kRawOffset,
  kRawSize,
  /*
   *  RawOffset and RawSize are only valid for watchers where perf_event_open
   *  will generate raw event data.  Raw data is sent as a byte buffer, so
   *  the offset and size are needed to extract the desired (integral!) value
   *  from the buffer.
   */
  kEvent,
  /*
   *  The name of the watcher, such as 'sAlloc'.
   *  Also used in tracepoints to define the specific tracepoint (as opposed
   *  to the group).  In this mode, may also be given as a `group:event` tuple
   *  delimited by a ':' (as is typical for other tools).
   */
  kFrequency,
  /*
   *  Specifies that the given watcher will be configured in perf_events with
   *  frequency mode (as opposed to periodic sampling).  Mutually exclusive
   *  with `Period`, presence of both should return error.  Presence of neither
   *  defaults to `period=1`.
   */
  kGroup,
  /*
   *  When `Event` is given and is not a valid "normal" event (such as
   *  `sALLOC`) nor a `group:event` tuple, then `Group` is needed to define
   *  the groupname for the tracepoint.
   */
  kId,
  /*
   *  The Id for the tracepoint.  Customers usually do not know this and will
   *  not specify it; but sometimes debugfs/tracefs may be inaccessible even
   *  though probe points can be consumed through `perf_events`.  This setting
   *  allows users to perform instrumentation in such a configuration.
   */
  kLabel,
  /*
   *  An informative label which is forwarded to the UX, but does not have any
   *  direct relationship with how the watcher calls `perf_event_open()`.  This
   *  may however be used to aggregate watchers in a single ringbuffer (if they
   *  share the same label).
   */
  kMode,
  /*
   *  Specifies what to do with a sample when it is collected.  Comprised of a
   *  string; the presence or absence of certain characters defines the
   *  output mode:
   *    * 'M' or 'm' -- emit a metric
   *    * 'G' or 'g' -- emit a flamegraph (default)
   *    * 'A', 'a', or '*' -- emit all outputs
   */
  kParameter,
  /*
   *  A function parameter number. Is expanded into the correct register for
   *  the System-V procedure call ABI for the current architecture.
   */
  kPeriod,
  /*
   *  Gives the period for which to configure sampling.  Conflicts with
   *  `Frequency`--presence of both is an error.
   */
  kRegister,
  /*
   *  The `perf_event` register number to use for sample normalization.  In a
   *  future patch, the user will be able to use register names.
   */
};

struct EventConf {
  EventConfMode mode;

  uint64_t id;

  std::string eventname;
  std::string groupname;
  std::string label;

  EventConfValueSource value_source;
  uint8_t register_num;
  uint8_t raw_size;
  uint64_t raw_offset;
  double value_scale;

  EventConfCadenceType cad_type;
  int64_t cadence;

  void clear() { *this = EventConf{}; }
};

EventConf *EventConf_parse(const char *msg); // Provided by generated code
