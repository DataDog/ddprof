#pragma once

extern "C" {
#include "dwfl_internals.h"
}

#include "ddprof_file_info.hpp"
#include "dso.hpp"

namespace ddprof {
// From a dso object (and the matching file), attach the module to the dwfl
// object, return the associated Dwfl_Module
Dwfl_Module *update_module(Dwfl *dwfl, ProcessAddress_t pc, const Dso &dso,
                           const FileInfoValue &fileInfoValue);

} // namespace ddprof
