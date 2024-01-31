// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "create_elf.hpp"

#include "dso_hdr.hpp"
#include "logger.hpp"

#include <fcntl.h>

namespace ddprof {

UniqueElf create_elf_from_self() {
  elf_version(EV_CURRENT);

  const int exe_fd = ::open("/proc/self/exe", O_RDONLY);
  if (exe_fd == -1) {
    LG_WRN("Failed to open /prox/self/exe");
    return {};
  }

  UniqueElf elf{elf_begin(exe_fd, ELF_C_READ_MMAP, nullptr)};
  if (!elf) {
    LG_WRN("Failed to create elf from memory: %s", elf_errmsg(-1));
    return {};
  }

  if (elf_kind(elf.get()) != ELF_K_ELF) {
    LG_WRN("Invalid elf: /proc/self/exe");
    return {};
  }

  ::close(exe_fd);
  return elf;
}
} // namespace ddprof
