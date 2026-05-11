// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <vector>

struct ddog_prof_Mapping2;
using ddog_prof_MappingId2 = ddog_prof_Mapping2 *;

namespace ddprof {

// Interned mapping handles. Each entry is a ddog_prof_MappingId2 (opaque
// pointer into the ProfilesDictionary). MapInfoIdx_t indexes into this vector.
using MapInfoTable = std::vector<ddog_prof_MappingId2>;

} // namespace ddprof
