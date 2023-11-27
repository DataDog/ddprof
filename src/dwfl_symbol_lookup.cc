// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwfl_symbol_lookup.hpp"

#include "ddprof_module.hpp"
#include "dwarf_helpers.hpp"
#include "dwfl_hdr.hpp"
#include "dwfl_internals.hpp"
#include "dwfl_symbol.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <numeric> // For std::iota
#include <queue>
#include <set>
#include <string>

#define DEBUG

namespace ddprof {

namespace {
struct DieInformation {
  struct Function {
    ElfAddress_t start_addr{};
    ElfAddress_t end_addr{};
    const char *func_name{};
    const char *file_name{};
    int line_number{0};
    int parent_pos{-1}; // position within the die vector
    SymbolIdx_t symbol_idx = -1;
  };
  std::vector<Function> die_mem_vec{};
};

struct DieSearchParam {
  Dwarf_Addr addr;
  Dwarf_Die *die_mem;
};

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

// Retrieve existing symbol or attempt to read from dwarf
void DwflSymbolLookup::get_or_insert(Dwfl *dwfl, const DDProfMod &ddprof_mod,
                                     SymbolTable &table,
                                     DsoSymbolLookup &dso_symbol_lookup,
                                     FileInfoId_t file_info_id,
                                     ProcessAddress_t process_pc,
                                     const Dso &dso,
                                     std::vector<SymbolIdx_t> &symbol_indices) {
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
    // add the inlined symbols on top
    get_inlined(symbol_wrapper, elf_pc, *find_res.first, symbol_indices);
    // then add the elf symbol
    symbol_indices.push_back(find_res.first->second.get_symbol_idx());
  } else {
    // insert symbols using elf info
    SymbolMap::ValueType &elf_sym =
        insert(dwfl, ddprof_mod, table, dso_symbol_lookup, process_pc, dso,
               symbol_wrapper);
    // parse associated dwarf info
    insert_inlining_info(dwfl, ddprof_mod, table, process_pc, dso,
                         symbol_wrapper, elf_sym);
    get_inlined(symbol_wrapper, elf_pc, elf_sym, symbol_indices);
    // add elf symbol after
    symbol_indices.push_back(elf_sym.second.get_symbol_idx());
  }
  return;
}

/* die_find callback for non-inlined function search */
static int die_search_func_cb(Dwarf_Die *fn_die, void *data) {
  DieSearchParam *ad = reinterpret_cast<DieSearchParam *>(data);
  if (dwarf_tag(fn_die) == DW_TAG_subprogram && dwarf_haspc(fn_die, ad->addr)) {
    memcpy(ad->die_mem, fn_die, sizeof(Dwarf_Die));
    return DWARF_CB_ABORT;
  }
  return DWARF_CB_OK;
}

Dwarf_Die *die_find_realfunc(Dwarf_Die *cu_die, Dwarf_Addr addr,
                             Dwarf_Die *die_mem) {
  DieSearchParam ad;
  ad.addr = addr;
  ad.die_mem = die_mem;
  /* dwarf_getscopes can't find subprogram. */
  if (!dwarf_getfuncs(cu_die, die_search_func_cb, &ad, 0))
    return NULL;
  else
    return die_mem;
}

// return index to added element, else returns -1
static int store_die_information(Dwarf_Die *sc_die, int parent_index,
                                 DieInformation &data,
                                 Dwarf_Files *dwarf_files) {
  dwarf_getattrs(sc_die, print_attribute, nullptr, 0);

  // function or inlined function
  DieInformation::Function function{};
  // die_name is usually the raw function name (no mangling info)
  // link name can have mangling info
  function.func_name = dwarf_diename(sc_die);
  LG_DBG("Storing func = %s", function.func_name);
  Dwarf_Attribute attr_mem;
  Dwarf_Attribute *attr;
  if ((attr = dwarf_attr(sc_die, DW_AT_low_pc, &attr_mem))) {
    if (attr) {
      Dwarf_Addr ret_value;
      if (dwarf_formaddr(attr, &ret_value) == 0) {
        function.start_addr = ret_value;
      }
    }
  }
  // end is stored as a unsigned (not as a pointer)
  if ((attr = dwarf_attr(sc_die, DW_AT_high_pc, &attr_mem))) {
    if (attr) {
      Dwarf_Word return_uval;
      if (dwarf_formudata(attr, &return_uval) == 0) {
        function.end_addr = function.start_addr + return_uval;
      }
    }
  }
  // some of the functions don't have the start and end info
  if (!function.start_addr || !function.end_addr) {
    return -1;
  }

  // for inlined functions, we do not often get decl tag
  if (dwarf_files &&
      ((attr = dwarf_attr(sc_die, DW_AT_decl_file, &attr_mem)) ||
       (attr = dwarf_attr(sc_die, DW_AT_call_file, &attr_mem)))) {
    Dwarf_Word fileIdx = 0;
    if (dwarf_formudata(attr, &fileIdx) == 0) {
      // Assuming 'files' is a structure holding file information
      // for the current compilation unit, obtained beforehand
      const char *file = dwarf_filesrc(dwarf_files, fileIdx, NULL, NULL);
      // Store or process the file name
      function.file_name = file;
      LG_DBG("File associated with function: %s", file);
    }
  }

  if ((attr = dwarf_attr(sc_die, DW_AT_decl_line, &attr_mem)) ||
      (attr = dwarf_attr(sc_die, DW_AT_call_line, &attr_mem))) {
    Dwarf_Word return_uval;
    if (dwarf_formudata(attr, &return_uval) == 0) {
      function.line_number = return_uval;
      LG_DBG("Line number associated with function: %u", function.line_number);
    }
  }

  // we often can find duplicates within the dwarf information
  function.parent_pos = parent_index;
  data.die_mem_vec.push_back(std::move(function));
  return (data.die_mem_vec.size() - 1);
}

static Dwarf_Die *find_functions_in_child_die(Dwarf_Die *current_die,
                                              int parent_index,
                                              DieInformation &die_info,
                                              Dwarf_Die *die_mem,
                                              Dwarf_Files *dwarf_files) {
  Dwarf_Die child_die;
  int ret;
  ret = dwarf_child(current_die, die_mem);
  if (ret != 0)
    return nullptr;
  do {
    int tag_val = dwarf_tag(die_mem);
    int next_parent_idx = parent_index;
    if (tag_val == DW_TAG_subprogram || tag_val == DW_TAG_inlined_subroutine) {
      int current_idx =
          store_die_information(die_mem, parent_index, die_info, dwarf_files);
      next_parent_idx = (current_idx != -1 ? current_idx : next_parent_idx);
    }
    //
    // todo: optimize the exploration to avoid going through soo many elements
    // Child dies can have functions, even without being a child of another func
    find_functions_in_child_die(die_mem, next_parent_idx, die_info, &child_die,
                                dwarf_files);
  } while (dwarf_siblingof(die_mem, die_mem) == 0);
  return nullptr;
}

static DDRes parse_die_information(Dwarf_Die *cudie, ElfAddress_t elf_addr,
                                   DieInformation &die_information) {
  Dwarf_Files *files = nullptr;
  size_t nfiles = 0;
  if (cudie == nullptr) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_NO_DWARF, "Unable to retrieve cu die");
  }
  // cached within the CU
  if (dwarf_getsrcfiles(cudie, &files, &nfiles) != 0) {
    files = nullptr;
  }
  Dwarf_Die die_mem;
  Dwarf_Die *sc_die = die_find_realfunc(cudie, elf_addr, &die_mem);
  if (sc_die == nullptr) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_DWFL_LIB_ERROR, "Unable to retrieve sc_die");
  }
  // store parent function at index 0
  if (store_die_information(sc_die, -1, die_information, files) == -1) {
    LG_DBG("Unable to store the parent function");
    // On some functions we are unable to find start / end info
    return ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
  }
  find_functions_in_child_die(sc_die, 0, die_information, &die_mem, files);

  for (auto &el : die_information.die_mem_vec) {
    LG_DBG("Inlined func start=%lx / end=%lx / Sym = %s / parent=%d",
           el.start_addr, el.end_addr, el.func_name, el.parent_pos);
  }
  return {};
}

static DDRes parse_lines(Dwarf_Die *cudie, const DDProfMod &mod,
                         ProcessAddress_t process_pc, SymbolMap &func_map,
                         DwflSymbolLookup::LineMap &line_map,
                         SymbolTable &table, DieInformation &die_information) {

  Dwarf_Lines *lines;
  size_t nlines;
  if (dwarf_getsrclines(cudie, &lines, &nlines) != 0) {
    LG_DBG("Unable to find source lines");
    return ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
  }
  for (size_t func_pos = 0; func_pos < die_information.die_mem_vec.size();
       ++func_pos) {
    const DieInformation::Function &current_func =
        die_information.die_mem_vec[func_pos];

    int start_index =
        binary_search_start_index(lines, nlines, current_func.start_addr);
    // retrieve dwarf source line
    Symbol &ref_sym = table[current_func.symbol_idx];
    Dwarf_Line *line = dwarf_onesrcline(lines, start_index);
    int lineno;
    if (dwarf_lineno(line, &lineno) == -1) {
      lineno = 0;
    } else {
      ref_sym._func_start_lineno = lineno;
    }
    const char *current_file = dwarf_linesrc(line, nullptr, nullptr);
    if (current_file) {
      ref_sym._srcpath = std::string(current_file);
    }
  }
  // todo loop on all lines to get real line numbers and cache them
  //  auto it = line_map.begin();
  //  line_map.insert(it,
  //                  std::pair<ElfAddress_t, DwflSymbolLookup::Line>(
  //                      static_cast<ElfAddress_t>(current_func.end_addr),
  //                      DwflSymbolLookup::Line{static_cast<uint32_t>(lineno),
  //                                             current_func.end_addr,
  //                                             current_func.symbol_idx}));

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
    parent_sym._srcpath = dso.format_filename();
    return ddres_warn(DD_WHAT_NO_DWARF);
  }
  ElfAddress_t elf_addr = process_pc - bias;
  SymbolMap &func_map = symbol_wrapper._symbol_map;
  DieInformation die_information;
  if (!IsDDResOK(parse_die_information(cudie, elf_addr, die_information)) ||
      die_information.die_mem_vec.size() == 0) {
    Symbol &parent_sym = table[parent_sym_idx];
    LG_DBG("Error when parsing die information for %s (%s)",
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
    table.emplace_back(
        Symbol({}, current_func.func_name ? current_func.func_name : "undef",
               current_func.line_number,
               current_func.file_name ? current_func.file_name : ""));
    current_func.symbol_idx = table.size() - 1;
    // add to the lookup
    LG_DBG("adding %lx - %lx: %s", current_func.start_addr,
           current_func.end_addr, current_func.func_name);
    inline_map.emplace(
        NestedSymbolKey{current_func.start_addr, current_func.end_addr},
        NestedSymbolValue(current_func.symbol_idx));
  }

  // associate line information to die information (includes file info)
  LineMap &line_map = symbol_wrapper._line_map;
  if (IsDDResNotOK(parse_lines(cudie, ddprof_mod, process_pc, func_map,
                               line_map, table, die_information))) {
    LG_DBG("Error when parsing line information (%s)", dso._filename.c_str());
  }

  for (unsigned pos = 0; pos < die_information.die_mem_vec.size(); ++pos) {
    DieInformation::Function &func = die_information.die_mem_vec[pos];
    auto &sym = table[func.symbol_idx];
    if (sym._srcpath.empty()) {
      // override with info from dso (this slightly mixes mappings and sources)
      // But it helps a lot at Datadog (as mappings are ignored for now in UI)
      sym._srcpath = dso.format_filename();
    }
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
#ifdef DEBUG
    LG_NTC("Insert (dwfl failure): %lx,%lx -> %s,%d,%s", start_sym, end_sym,
           table[symbol_idx]._symname.c_str(), symbol_idx,
           dso.to_string().c_str());
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

void DwflSymbolLookup::get_inlined(const SymbolWrapper &symbol_wrapper,
                                   ElfAddress_t elf_pc,
                                   const SymbolMap::ValueType &parent_sym,
                                   std::vector<SymbolIdx_t> &inlined_symbols) {
  const InlineMap &inline_map = symbol_wrapper._inline_map;

  NestedSymbolKey parent_key{parent_sym.first, parent_sym.second.get_end()};
  NestedSymbolMap::FindRes find_res =
      inline_map.find_closest(elf_pc, parent_key);
  LG_DBG("Looking for %lx (%lu)- matched %lx - %lx\n", elf_pc,
         inline_map.size(), find_res.second ? find_res.first->first.start : 0,
         find_res.second ? find_res.first->first.end : 0);

  while (find_res.second) {
    inlined_symbols.push_back(find_res.first->second.get_symbol_idx());
    find_res = inline_map.find_parent(find_res.first, parent_key, elf_pc);
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

} // namespace ddprof
