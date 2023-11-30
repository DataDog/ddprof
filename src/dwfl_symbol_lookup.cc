// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_symbol_lookup.hpp"

#include "dwarf_helpers.hpp"
#include "dwfl_symbol.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <numeric> // For std::iota
#include <queue>
#include <set>
#include <string_view>

#define DEBUG

namespace ddprof {

namespace {

size_t binary_search_start_index(Dwarf_Lines *lines, size_t nlines,
                                 ElfAddress_t start_sym) {
  size_t low = 0;
  size_t high = nlines - 1;

  while (low <= high) {
    size_t mid = low + (high - low) / 2;
    Dwarf_Line *mid_line = dwarf_onesrcline(lines, mid);
    Dwarf_Addr mid_addr;
    dwarf_lineaddr(mid_line, &mid_addr);

    if (mid_addr < start_sym) {
      low = mid + 1;
    } else if (mid_addr > start_sym) {
      high = mid - 1;
    } else {
      return mid;
    }

    if (low == high) {
      return low;
    }
  }

  return nlines; // Return a default value if no suitable index is found
}
} // namespace

DwflSymbolLookup::DwflSymbolLookup() {
  if (const char *env_p = std::getenv("DDPROF_CACHE_SETTING")) {
    if (strcmp(env_p, "VALIDATE") == 0) {
      // Allows to compare the accuracy of the cache
      _lookup_setting = K_CACHE_VALIDATE;
      LG_NTC("%s : Validate the cache data at every call", __FUNCTION__);
    } else {
      LG_WRN("%s : ignoring DDPROF_CACHE_SETTING value %s", __FUNCTION__,
             env_p);
    }
  }
}

unsigned DwflSymbolLookup::size() const {
  unsigned total_nb_elts = 0;
  std::for_each(_file_info_function_map.begin(), _file_info_function_map.end(),
                [&](FileInfo2SymbolVT const &el) {
                  total_nb_elts += el.second._symbol_map.size();
                });
  return total_nb_elts;
}

void DwflSymbolLookup::add_fun_loc(
    DwflSymbolLookup::SymbolWrapper &symbol_wrapper,
    const SymbolMap::ValueType &parent_sym, ElfAddress_t elf_pc,
    ProcessAddress_t process_pc, std::vector<FunLoc> &func_locs) {
  auto last_inlined =
      get_inlined(symbol_wrapper, process_pc, elf_pc, parent_sym, func_locs);
  uint32_t line = 0;
  if (last_inlined.second) {
    line = last_inlined.first->second.get_call_line_number();
  } else {
    // line can be associated to parent
    const auto line_find = symbol_wrapper._line_map.find_closest(elf_pc);
    if (line_find.second) {
      line = line_find.first->second.get_symbol_idx();
    }
  }
  LG_DBG("Adding parent = %d", parent_sym.second.get_symbol_idx());
  func_locs.emplace_back(
      FunLoc{._ip = process_pc,
             ._lineno = line,
             ._symbol_idx = parent_sym.second.get_symbol_idx(),
             ._map_info_idx = -1});
}

// Retrieve existing symbol or attempt to read from dwarf
void DwflSymbolLookup::get_or_insert(Dwfl *dwfl, const DDProfMod &ddprof_mod,
                                     SymbolTable &table,
                                     DsoSymbolLookup &dso_symbol_lookup,
                                     FileInfoId_t file_info_id,
                                     ProcessAddress_t process_pc,
                                     const Dso &dso,
                                     std::vector<FunLoc> &func_locs) {
  ++_stats._calls;
  ElfAddress_t const elf_pc = process_pc - ddprof_mod._sym_bias;
#ifdef DEBUG
  LG_DBG("Looking for : %lx = (%lx - %lx) / dso:%s", elf_pc, process_pc,
         ddprof_mod._low_addr, dso._filename.c_str());
#endif
  SymbolWrapper &symbol_wrapper = _file_info_function_map[file_info_id];
  SymbolMap &map = symbol_wrapper._symbol_map;
  SymbolMap::FindRes const find_res = map.find_closest(elf_pc);
  if (find_res.second) { // already found the correct symbol
#ifdef DEBUG
    LG_DBG("Match: %lx,%lx -> %s,%d", find_res.first->first,
           find_res.first->second.get_end(),
           table[find_res.first->second.get_symbol_idx()]._symname.c_str(),
           find_res.first->second.get_symbol_idx());
#endif
    // cache validation mechanism: force dwfl lookup to compare with matched
    // symbols
    if (_lookup_setting == K_CACHE_VALIDATE) {
      if (symbol_lookup_check(ddprof_mod._mod, process_pc,
                              table[find_res.first->second.get_symbol_idx()])) {
        ++_stats._errors;
      }
    }
    ++_stats._hit;
    add_fun_loc(symbol_wrapper, *find_res.first, elf_pc, process_pc, func_locs);
  } else {
    const size_t previous_table_size = table.size();
    // insert symbols using elf info
    SymbolMap::ValueType &elf_sym =
        insert(dwfl, ddprof_mod, table, dso_symbol_lookup, process_pc, dso,
               symbol_wrapper);
    // parse associated dwarf info
    insert_inlining_info(dwfl, ddprof_mod, table, process_pc, dso,
                         symbol_wrapper, elf_sym);
    // For newly added symbols, insure we don't leave a blank file name
    for (unsigned i = previous_table_size; i < table.size(); ++i) {
      auto &sym = table[i];
      if (sym._srcpath.empty()) {
        // override with info from dso (this slightly mixes mappings and
        // sources) But it helps a lot at Datadog (as mappings are ignored for
        // now in UI)
        sym._srcpath = dso.format_filename();
      }
    }
    add_fun_loc(symbol_wrapper, elf_sym, elf_pc, process_pc, func_locs);
  }
  return;
}

static DDRes parse_lines(Dwarf_Die *cudie, const DDProfMod &mod,
                         DwflSymbolLookup::SymbolWrapper &symbol_wrapper,
                         SymbolTable &table, DieInformation &die_information) {

  DwflSymbolLookup::LineMap &line_map = symbol_wrapper._line_map;
  DwflSymbolLookup::InlineMap &inline_map = symbol_wrapper._inline_map;
  Dwarf_Lines *lines;
  size_t nlines;
  const DieInformation::Function &parent_func = die_information.die_mem_vec[0];
  SymbolIdx_t symbol_idx = parent_func.symbol_idx;
  const Symbol *ref_sym = &table[symbol_idx];

  if (dwarf_getsrclines(cudie, &lines, &nlines) != 0) {
    LG_DBG("Unable to find source lines for %s", ref_sym->_symname.c_str());
    return ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
  }
  NestedSymbolKey parent_bound{parent_func.start_addr, parent_func.end_addr};
  size_t start_index =
      binary_search_start_index(lines, nlines, parent_bound.start);
  if (start_index >= nlines) {
    LG_DBG("Unable to match lines for %s", ref_sym->_symname.c_str());
    return ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
  }
  auto hint_line = line_map.end();
  NestedSymbolMap::ConstIt hint_inline = inline_map.begin();
  Dwarf_Addr previous_addr = 0;
  NestedSymbolMap::FindRes current_func{inline_map.end(), false};
  // store closest line per file (to avoid missmatches)
  std::unordered_map<std::string, int> closest_lines;
  for (size_t line_index = start_index; line_index < nlines; ++line_index) {
    Dwarf_Line *line = dwarf_onesrcline(lines, line_index);
    Dwarf_Addr line_addr;
    dwarf_lineaddr(line, &line_addr);
    if (line_addr > parent_bound.end) {
      break;
    }
    int lineno;
    if (dwarf_lineno(line, &lineno) == -1) {
      lineno = 0; // Handle the case where line number is not available
    }
    // Update the source path if necessary
    const char *current_file = dwarf_linesrc(line, nullptr, nullptr);

    if (previous_addr && line_addr != previous_addr) {
      if (hint_line != line_map.end() &&
          hint_line->second.get_symbol_idx() ==
              closest_lines[ref_sym->_srcpath]) {
        // extend previous element
        hint_line->second.set_end(previous_addr);
      } else {
        // New line element
        hint_line = line_map.emplace_hint(
            hint_line,
            std::make_pair(
                previous_addr,
                SymbolSpan{line_addr - 1, closest_lines[ref_sym->_srcpath]}));
      }
#ifdef DEBUG
      LG_DBG("Associate %d (%lx->%lx) / %s to %s (vs %s)",
             closest_lines[ref_sym->_srcpath], previous_addr, line_addr - 1,
             current_file ? current_file : "undef",
             ref_sym->_demangle_name.c_str(), ref_sym->_srcpath.c_str());
#endif
      // todo: this can be more efficient with hint
      current_func = inline_map.find_closest(line_addr, parent_bound);
      if (!current_func.second) {
        symbol_idx = parent_func.symbol_idx;
      } else {
        symbol_idx = current_func.first->second.get_symbol_idx();
        hint_inline = current_func.first;
      }
      ref_sym = &table[symbol_idx];
    }
    // keep line, if it matches the symbol
    // todo can be optimized to avoid conversion
    closest_lines[std::string(current_file)] = lineno;
    previous_addr = line_addr;
  }
  return {};
}

DDRes DwflSymbolLookup::insert_inlining_info(
    Dwfl *dwfl, const DDProfMod &ddprof_mod, SymbolTable &table,
    ProcessAddress_t process_pc, const Dso &dso, SymbolWrapper &symbol_wrapper,
    SymbolMap::ValueType &parent_func) {

  SymbolIdx_t parent_sym_idx = parent_func.second.get_symbol_idx();
  Dwarf_Addr bias;
  Dwarf_Die *cudie = dwfl_addrdie(dwfl, process_pc, &bias);
  if (!cudie) {
    Symbol &parent_sym = table[parent_sym_idx];
    LG_DBG("No debug information for %s (%s)",
           parent_sym._demangle_name.c_str(), dso._filename.c_str());
    return ddres_warn(DD_WHAT_NO_DWARF);
  }
  ElfAddress_t elf_addr = process_pc - bias;
  DieInformation die_information;
  if (!IsDDResOK(parse_die_information(cudie, elf_addr, die_information)) ||
      die_information.die_mem_vec.size() == 0) {
    Symbol &parent_sym = table[parent_sym_idx];
    LG_DBG("Unable to extract die information for %s (%s)",
           parent_sym._demangle_name.c_str(), dso._filename.c_str());
    return ddres_warn(DD_WHAT_NO_DWARF);
  }

  // Extend the span of the elf symbol
  if ((parent_func.second.get_end() + 1) <
      die_information.die_mem_vec[0].end_addr) {
    LG_DBG("Extending end of parent func from %lx to %lx",
           parent_func.second.get_end(),
           die_information.die_mem_vec[0].end_addr);
    parent_func.second.set_end(die_information.die_mem_vec[0].end_addr);
  }
  die_information.die_mem_vec[0].symbol_idx = parent_sym_idx;

  // update parent file name
  if (die_information.die_mem_vec[0].file_name) {
    auto &sym = table[parent_sym_idx];
    sym._srcpath = die_information.die_mem_vec[0].file_name;
  }

  NestedSymbolMap &inline_map = symbol_wrapper._inline_map;
  for (unsigned pos = 1; pos < die_information.die_mem_vec.size(); ++pos) {
    DieInformation::Function &current_func = die_information.die_mem_vec[pos];
    current_func.symbol_idx = table.size();
    table.emplace_back(
        Symbol({}, current_func.func_name ? current_func.func_name : "undef",
               current_func.decl_line_number,
               current_func.file_name ? current_func.file_name : ""));
    // add to the lookup
    inline_map.emplace(
        NestedSymbolKey{current_func.start_addr, current_func.end_addr},
        NestedSymbolValue(current_func.symbol_idx,
                          current_func.call_line_number));
  }

  // associate line information to die information (includes file info)
  if (IsDDResNotOK(parse_lines(cudie, ddprof_mod, symbol_wrapper, table,
                               die_information))) {
    LG_DBG("Error when parsing line information (%s)", dso._filename.c_str());
  }
  return {};
}

SymbolMap::ValueType &
DwflSymbolLookup::insert(Dwfl *dwfl, const DDProfMod &ddprof_mod,
                         SymbolTable &table, DsoSymbolLookup &dso_symbol_lookup,
                         ProcessAddress_t process_pc, const Dso &dso,
                         SymbolWrapper &symbol_wrapper) {
  Symbol symbol;
  GElf_Sym elf_sym;
  Offset_t lbias;
  SymbolMap &func_map = symbol_wrapper._symbol_map;

  ElfAddress_t const elf_pc = process_pc - ddprof_mod._sym_bias;
  if (!symbol_get_from_dwfl(ddprof_mod._mod, process_pc, symbol, elf_sym,
                            lbias)) {
    ++_stats._no_dwfl_symbols;
    // Override with info from dso
    // Avoid bouncing on these requests and insert an element
    Offset_t start_sym = elf_pc;
    Offset_t const end_sym = start_sym + 1; // minimum range
// #define ADD_ADDR_IN_SYMB // creates more elements (but adds info on
// addresses)
#ifdef ADD_ADDR_IN_SYMB
    // adds interesting debug information that can be used to investigate
    // symbolization failures. Also causes memory increase
    SymbolIdx_t symbol_idx =
        dso_symbol_lookup.get_or_insert(elf_pc, dso, table);
#else
    SymbolIdx_t const symbol_idx = dso_symbol_lookup.get_or_insert(dso, table);
#endif
    auto res_emplace =
        func_map.emplace(start_sym, SymbolSpan(end_sym, symbol_idx));
    assert(res_emplace.second);
    return *(res_emplace.first);
  }

  if (lbias != ddprof_mod._sym_bias) {
    LG_NTC("Failed (PID%d) assumption %s - %lx != %lx", dso._pid,
           dso._filename.c_str(), lbias, ddprof_mod._sym_bias);
    assert(0);
  }

  {
    ElfAddress_t start_sym;
    ElfAddress_t end_sym;
    // All paths bellow will insert symbol in the table
    SymbolIdx_t const symbol_idx = table.size();
    table.push_back(std::move(symbol));

    Symbol &sym_ref = table.back();
    if (sym_ref._srcpath.empty()) {}

    if (!compute_elf_range(elf_pc, elf_sym, start_sym, end_sym)) {
      // elf section does not add up to something that makes sense
      // insert this PC without considering elf section
      start_sym = elf_pc;
      end_sym = elf_pc;
      LG_DBG("elf_range failure --> Insert: %lx,%lx -> %s, %d / shndx=%d",
             start_sym, end_sym, sym_ref._symname.c_str(), symbol_idx,
             elf_sym.st_shndx);
    }
#define DEBUG
#ifdef DEBUG
    LG_DBG("-------------------------------");
    LG_DBG("Insert: %lx,%lx -> %s,%d / shndx=%d", start_sym, end_sym,
           sym_ref._symname.c_str(), symbol_idx, elf_sym.st_shndx);
#endif
    auto res_emplace =
        func_map.emplace(start_sym, SymbolSpan(end_sym, symbol_idx));
    assert(res_emplace.second);
    return *(res_emplace.first);
  }
}

NestedSymbolMap::FindRes DwflSymbolLookup::get_inlined(
    SymbolWrapper &symbol_wrapper, ElfAddress_t process_pc, ElfAddress_t elf_pc,
    const SymbolMap::ValueType &parent_sym, std::vector<FunLoc> &func_locs) {
  const InlineMap &inline_map = symbol_wrapper._inline_map;

  NestedSymbolKey parent_key{parent_sym.first, parent_sym.second.get_end()};
  NestedSymbolMap::FindRes find_inline =
      inline_map.find_closest(elf_pc, parent_key);
  NestedSymbolMap::FindRes last_found = {inline_map.end(), false};
  while (find_inline.second) {
    uint32_t line = 0;
    if (last_found.second) {
      line = last_found.first->second.get_call_line_number();
    } else {
      auto find_line = symbol_wrapper._line_map.find_closest(elf_pc);
      if (find_line.second) {
        line = find_line.first->second.get_symbol_idx();
      }
    }
    func_locs.emplace_back(
        FunLoc{._ip = process_pc,
               ._lineno = line,
               ._symbol_idx = find_inline.first->second.get_symbol_idx(),
               ._map_info_idx = -1});
    find_inline = inline_map.find_parent(find_inline.first, parent_key, elf_pc);
    last_found = find_inline;
  }
  return last_found;
}

bool DwflSymbolLookup::symbol_lookup_check(Dwfl_Module *mod,
                                           Dwarf_Addr process_pc,
                                           const Symbol &symbol) {
  GElf_Off loffset;
  GElf_Sym lsym;
  GElf_Word lshndxp;
  Elf *lelfp;
  Dwarf_Addr lbias;

  const char *localsymname = dwfl_module_addrinfo(
      mod, process_pc, &loffset, &lsym, &lshndxp, &lelfp, &lbias);

#ifdef DEBUG
  LG_DBG("DWFL: Lookup res = %lx->%lx, shndx=%u, biais=%lx, elfp=%p, "
         "shndxp=%u, %s",
         lsym.st_value, lsym.st_value + lsym.st_size, lsym.st_shndx, lbias,
         lelfp, lshndxp, localsymname);
#endif

  bool error_found = false;
  if (!localsymname) { // symbol failure no use checking
    return error_found;
  }

  if (symbol._symname.empty()) {
    LG_ERR("Error from cache : non null symname = %s", localsymname);
  } else {
    if (strcmp(symbol._symname.c_str(), localsymname) != 0) {
      LG_ERR("Error from cache symname Real=%s vs Cache=%s ", localsymname,
             symbol._symname.c_str());
      error_found = true;
    }
    if (error_found) {
      LG_ERR("symname = %s\n", symbol._symname.c_str());
    }
  }
  return error_found;
}

void DwflSymbolLookupStats::display(unsigned nb_elts) const {
  static const int k_cent_precision = 10000;
  if (_calls) {
    LG_NTC("DWFL_SYMB | %10s | [%d/%d] = %ld", "Hit", _hit, _calls,
           (static_cast<int64_t>(_hit) * k_cent_precision) / _calls);
    if (_errors) {
      LG_WRN("DWFL_SYMB | %10s | [%d/%d] = %ld", "Errors", _errors, _calls,
             (static_cast<int64_t>(_errors) * k_cent_precision) / _calls);
    }
    if (_no_dwfl_symbols) {
      LG_NTC("DWFL_SYMB | %10s | [%d/%d] = %d", "Not found", _no_dwfl_symbols,
             _calls, (_no_dwfl_symbols * k_cent_precision) / _calls);
    }
    LG_NTC("DWFL_SYMB | %10s | %d", "Size ", nb_elts);
  } else {
    LG_NTC("DWFL_SYMB NO CALLS");
  }
}

void DwflSymbolLookupStats::reset() {
  _hit = 0;
  _calls = 0;
  _errors = 0;
}

} // namespace ddprof
