// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind_state.hpp"

#include "create_elf.hpp"
#include "logger.hpp"

namespace ddprof {
std::optional<UnwindState> create_unwind_state(int dd_profiling_fd,
                                               bool disable_symbolization) {
  auto elf = create_elf_from_self();
  if (!elf) {
    return std::nullopt;
  }

  return UnwindState(std::move(elf), dd_profiling_fd, disable_symbolization);
}
} // namespace ddprof