#pragma once

#include "ddres.hpp"

#include <stdint.h>

namespace ddprof {

static constexpr uint32_t k_header_magic = 0x4A695444;
static constexpr uint32_t k_header_magic_rev = 0x4454694A;

struct JITHeader {
  uint32_t magic;	/* characters "jItD" */
  uint32_t version;	/* header version */
  uint32_t total_size;	/* total size of header */
  uint32_t elf_mach;	/* elf mach target */
  uint32_t pad1;	/* reserved */
  uint32_t pid;		/* JIT process id */
  uint64_t timestamp;	/* timestamp */
  uint64_t flags;	/* flags */
};

DDRes jit_read(std::string_view file);

}
