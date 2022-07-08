#pragma once

#include "ddprof_file_info.hpp"
#include "ddprof_module.hpp"
#include "ddres_def.hpp"
#include "dso.hpp"
#include "dwfl_internals.hpp"

namespace ddprof {
// From a dso object (and the matching file), attach the module to the dwfl
// object, return the associated Dwfl_Module
DDRes update_module(Dwfl *dwfl, ProcessAddress_t pc,
                    const DDProfModRange &mod_range,
                    const FileInfoValue &fileInfoValue, DDProfMod &ddprof_mod);

DDRes update_bias(DDProfMod &ddprof_mod);
} // namespace ddprof
