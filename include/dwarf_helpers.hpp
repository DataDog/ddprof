#pragma once

#include "ddprof_defs.hpp"
#include "ddres_def.hpp"
#include "dwfl_internals.hpp"

#include <vector>

namespace ddprof {
struct DieInformation {
  struct Function {
    ElfAddress_t start_addr{};
    ElfAddress_t end_addr{};
    const char *func_name{};
    const char *file_name{};
    int decl_line_number{0};
    int call_line_number{0};
    int parent_pos{-1}; // position within the die vector
    SymbolIdx_t symbol_idx = -1;
  };
  std::vector<Function> die_mem_vec{};
};

// debug attribute functions
const char *get_attribute_name(int attrCode);
int print_attribute(Dwarf_Attribute *attr, void *arg);

DDRes parse_die_information(Dwarf_Die *cudie, ElfAddress_t elf_addr,
                            DieInformation &die_information);
} // namespace ddprof
