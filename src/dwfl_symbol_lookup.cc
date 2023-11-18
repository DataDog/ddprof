// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_symbol_lookup.hpp"

#include "ddprof_module.hpp"
#include "dwfl_hdr.hpp"
#include "dwfl_internals.hpp"
#include "dwfl_symbol.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>

namespace ddprof {

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
  std::for_each(
      _file_info_function_map.begin(), _file_info_function_map.end(),
      [&](FileInfo2SymbolVT const &el) { total_nb_elts += el.second.size(); });
  return total_nb_elts;
}

/****************/
/* Range implem */
/****************/

// Retrieve existing symbol or attempt to read from dwarf
SymbolIdx_t DwflSymbolLookup::get_or_insert(
    Dwfl *dwfl, const DDProfMod &ddprof_mod, SymbolTable &table,
    DsoSymbolLookup &dso_symbol_lookup, FileInfoId_t file_info_id,
    ProcessAddress_t process_pc, const Dso &dso) {
  ++_stats._calls;
  ElfAddress_t const elf_pc = process_pc - ddprof_mod._sym_bias;

#ifdef DEBUG
  LG_DBG("Looking for : %lx = (%lx - %lx) / dso:%s", elf_pc, process_pc,
         ddprof_mod._low_addr, dso._filename.c_str());
#endif
  SymbolMap &map = _file_info_function_map[file_info_id];
  LineMap &line_map = _file_info_inlining_map[file_info_id];
  SymbolMap::FindRes const find_res = map.find_closest(elf_pc);
  if (find_res.second) { // already found the correct symbol
#ifdef DEBUG
    LG_DBG("Match : %lx,%lx -> %s,%d", find_res.first->first,
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
    return find_res.first->second.get_symbol_idx();
  }
  // todo get line no
  return insert(dwfl, ddprof_mod, table, dso_symbol_lookup, process_pc, dso,
                map, line_map);
}

SymbolIdx_t DwflSymbolLookup::insert(Dwfl *dwfl, const DDProfMod &ddprof_mod,
                                     SymbolTable &table,
                                     DsoSymbolLookup &dso_symbol_lookup,
                                     ProcessAddress_t process_pc,
                                     const Dso &dso, SymbolMap &func_map,
                                     LineMap &line_map) {

  Symbol symbol;
  GElf_Sym elf_sym;
  Offset_t lbias;

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
#ifdef DEBUG
    LG_NTC("Insert (dwfl failure): %lx,%lx -> %s,%d,%s", start_sym, end_sym,
           table[symbol_idx]._symname.c_str(), symbol_idx,
           dso.to_string().c_str());
#endif
    func_map.emplace(start_sym, SymbolSpan(end_sym, symbol_idx));
    return symbol_idx;
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

#ifdef DEBUG
    LG_DBG("Insert: %lx,%lx -> %s,%d / shndx=%d", start_sym, end_sym,
           sym_ref._symname.c_str(), symbol_idx, elf_sym.st_shndx);
#endif
    if (IsDDResNotOK(parse_lines(dwfl, ddprof_mod, process_pc, start_sym,
                                 end_sym, line_map, table, symbol_idx))) {
      LG_DBG("Error when parsing line information for %s (%s)",
             sym_ref._demangle_name.c_str(), dso._filename.c_str());
    }
    if (sym_ref._srcpath.empty()) {
      // override with info from dso (this slightly mixes mappings and sources)
      // But it helps a lot at Datadog (as mappings are ignored for now in UI)
      sym_ref._srcpath = dso.format_filename();
    }
    func_map.emplace(start_sym, SymbolSpan(end_sym, symbol_idx));
    return symbol_idx;
  }
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

  return 0; // Return a default value if no suitable index is found
}

DDRes DwflSymbolLookup::parse_lines(Dwfl *dwfl, const DDProfMod &mod,
                                    ProcessAddress_t process_pc,
                                    ElfAddress_t start_sym,
                                    ElfAddress_t end_sym, LineMap &line_map,
                                    SymbolTable &table,
                                    SymbolIdx_t current_sym) {
  Dwarf_Addr bias;
  // This will lazily load the dwarf file
  // the decompression can take time
  // todo use dw access to avoid opening PID times
  Dwarf_Die *cudie = dwfl_addrdie(dwfl, process_pc, &bias);
  Dwarf *dwarf = dwfl_module_getdwarf(mod._mod, &bias);
  //  ElfAddress_t elf_address = process_pc - bias;
  LG_DBG("Start %lx - end %lx", start_sym, end_sym);

  if (!cudie) {
    // This will fail in case of no debug info
    return ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
  }

  Dwarf_Lines *lines;
  size_t nlines;
  if (dwarf_getsrclines(cudie, &lines, &nlines) != 0) {
    LG_DBG("Unable to find source lines");
    return ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
  }
  auto it = line_map.begin();

  // for now we care about first source file
  const char *current_line_file = nullptr;

  size_t start_index = binary_search_start_index(lines, nlines, start_sym);
  int start_lineno = -1;
  int end_lineno = -1;
  {
    size_t end_index = binary_search_start_index(lines, nlines, end_sym);
    Dwarf_Line *line = dwarf_onesrcline(lines, end_index);

    if (dwarf_lineno(line, &end_lineno) == -1) {
      end_lineno = std::numeric_limits<int>::max();
    }
  }

  Symbol &ref_sym = table[current_sym];

  // todo: different files
  for (size_t i = start_index; i < nlines; ++i) {
    Dwarf_Line *line = dwarf_onesrcline(lines, i);
    Dwarf_Addr line_addr;
    int lineno;
    dwarf_lineaddr(line, &line_addr);
    if (line_addr < start_sym) {
      continue;
    }
    if (line_addr > end_sym) {
      break;
    }
    current_line_file = dwarf_linesrc(line, nullptr, nullptr);
    if (current_line_file && ref_sym._srcpath.empty()) {
      ref_sym._srcpath = std::string(current_line_file);
    }
    if (dwarf_lineno(line, &lineno) == -1) {
      lineno = 0;
    } else if (!ref_sym._func_start_lineno) {
      start_lineno = lineno;
      ref_sym._func_start_lineno = lineno;
    }
    // ... Process line information here ...
    LG_DBG("Dwarf file = %s / %d / %lx", current_line_file, lineno, line_addr);
    it = line_map.insert(it,
                         std::pair<ElfAddress_t, Line>(
                             static_cast<ElfAddress_t>(line_addr),
                             Line{static_cast<uint32_t>(lineno), line_addr}));
  }
  return {};
}

} // namespace ddprof
