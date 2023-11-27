#pragma once

#include "dwfl_internals.hpp"

#include <vector>

namespace ddprof {

// debug attribute functions
const char *get_attribute_name(int attrCode);
int print_attribute(Dwarf_Attribute *attr, void *arg);

} // namespace ddprof
