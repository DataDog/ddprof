// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "event_config.hpp"

EventConfMode &operator|=(EventConfMode &A, const EventConfMode &B) {
  A = static_cast<EventConfMode>(static_cast<unsigned>(A) |
                                 static_cast<unsigned>(B));
  return A;
}

EventConfMode operator&(const EventConfMode &A, const EventConfMode &B) {
  // & on bitmask enums is valid only in the space spanned by the values
  return static_cast<EventConfMode>(static_cast<uint64_t>(A) &
                                    static_cast<uint64_t>(B) &
                                    static_cast<uint64_t>(EventConfMode::kAll));
}

// Bitmask inclusion
bool operator<=(const EventConfMode A, const EventConfMode B) {
  return EventConfMode::kNone != ((EventConfMode::kAll & A) & B);
}
