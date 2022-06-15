// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "pevent.h"

bool pevent_include_kernel_events(
    [[maybe_unused]] const PEventHdr *pevent_hdr) {
  return false;
}
