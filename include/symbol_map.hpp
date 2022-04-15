// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

extern "C" {
#include "ddprof_defs.h"
}

#include <map>

// This is a quick and dirty implementation, which presumes simple value types,
// since they are copied to the left and right interval.
// Interval checks are done explicitly (i.e., x in `[a,b]` instead of `[a,b)`)
// Upon insertion, overlapping intervals are removed rather than split.
namespace ddprof {
template <typename VAL_T> 
class AddressMap {
public:
  AddressMap();
  insert(uint64_t key, VAL_T val);

private:
  std::map<uint64_t, VAL_T> _ranges;
}
}
