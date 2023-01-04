#pragma once

#include "ddprof_defs.hpp"

struct Elf;

bool get_elf_offsets(Elf *elf, const char *filepath, ElfAddress_t &vaddr,
                     Offset_t &bias_offset, Offset_t &text_base);

const char* get_section_data(Elf *elf, const char *section_name);


bool  process_fdes(Elf *elf);
