#pragma once

#include "ddprof_file_info.hpp"
#include "dso.hpp"
#include "dwfl_internals.hpp"
#include "ddprof_module.hpp"

namespace ddprof {
// From a dso object (and the matching file), attach the module to the dwfl
// object, return the associated Dwfl_Module
DDProfMod update_module(Dwfl *dwfl, ProcessAddress_t pc, const Dso &dso,  DDProfModRange mod_range,
                        const FileInfoValue &fileInfoValue);

} // namespace ddprof
