// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_file_info.hpp"
#include "ddprof_module.hpp"
#include "ddres_def.hpp"
#include "dso.hpp"
#include "dwfl_internals.hpp"

namespace ddprof {
// From a dso object (and the matching file), attach the module to the dwfl
// object, return the associated Dwfl_Module
DDRes report_module(Dwfl *dwfl, ProcessAddress_t pc, const Dso &dso,
                    const FileInfoValue &fileInfoValue, DDProfMod &ddprof_mod);

} // namespace ddprof
