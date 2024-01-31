// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <libelf.h>
#include <memory>

namespace ddprof {

inline constexpr auto elf_deleter = [](Elf *elf) { elf_end(elf); };
using UniqueElf = std::unique_ptr<Elf, decltype(elf_deleter)>;

UniqueElf create_elf_from_self();

} // namespace ddprof
