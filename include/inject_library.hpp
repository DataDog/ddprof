#pragma once

#include "ddres_def.hpp"

#include <string_view>
#include <unistd.h>

namespace ddprof {
DDRes inject_library(std::string_view lib_path, pid_t pid,
                     uint64_t &entry_payload_address,
                     uint64_t &exit_payload_address);
} // namespace ddprof
