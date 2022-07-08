#include <iostream>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "logger.hpp"
#include "runtime_symbol_lookup.hpp"

namespace ddprof {

struct managed_method_info {
public:
  managed_method_info(std::uint64_t addr, std::size_t cs, std::string fn)
      : address{addr}, code_size{cs}, function_name{std::move(fn)} {}

  std::uint64_t address;
  std::size_t code_size;
  std::string function_name;
};

static FILE *perfmaps_open(int pid, const char *path_to_perfmap = "") {
  char buf[1024] = {0};
  auto n = snprintf(buf, 1024, "%s/perf-%d.map", path_to_perfmap, pid);
  if (n >= 1024) { // unable to snprintf everything
    return nullptr;
  }
  return fopen(buf, "r");
}

bool should_skip_symbol(const char *symbol) {
  return strstr(symbol, "GenerateResolveStub") != nullptr ||
      strstr(symbol, "GenerateDispatchStub") != nullptr ||
      strstr(symbol, "GenerateLookupStub") != nullptr ||
      strstr(symbol, "AllocateTemporaryEntryPoints") != nullptr;
}

void RuntimeSymbolLookup::fill_perfmap_from_file(int pid, SymbolMap &symbol_map,
                                                 SymbolTable &symbol_table) {
  static const char spec[] = "%lx %x %[^\t\n]";
  FILE *pmf = perfmaps_open(pid, "/tmp");

  symbol_map.clear();
  if (pmf == nullptr) {
    // Add a single fake symbol to avoid bouncing
    symbol_map.emplace(0, RumtimeSymbolVal(1, -1));
    LG_DBG("No perfmap file found (PID%d)", pid);
    return;
  }

  char *line = NULL;
  size_t sz_buf = 0;
  char buffer[2048];
  auto it = symbol_map.end();
  while (-1 != getline(&line, &sz_buf, pmf)) {
    uint64_t address;
    uint32_t code_size;
    if (3 != sscanf(line, spec, &address, &code_size, buffer) ||
        should_skip_symbol(buffer)) {
      continue;
    }
    // elements are ordered
    it = symbol_map.emplace_hint(
        it, address,
        RumtimeSymbolVal(address + code_size - 1, symbol_table.size()));
    symbol_table.emplace_back(
        Symbol(std::string(buffer), std::string(buffer), 0, "unknown"));
  }

  fclose(pmf);
}

SymbolIdx_t RuntimeSymbolLookup::get_or_insert(pid_t pid, ProcessAddress_t pc,
                                               SymbolTable &symbol_table) {
  SymbolMap &symbol_map = _pid_map[pid];
  if (symbol_map.empty()) {
    fill_perfmap_from_file(pid, symbol_map, symbol_table);
    // TODO : how do we know we need to refresh ?
    // read the map
  }

  RuntimeSymbolFindRes find_res = find_closest(symbol_map, pc);
  if (find_res.second) {
    return find_res.first->second.get_symbol_idx();
  }
  // TODO what happens when we don't find a sym ? :-)
  LG_WRN("(PID%d) Unable to find a symbol for %lx", pid, pc);
  return 0;
}

RuntimeSymbolLookup::RuntimeSymbolFindRes
RuntimeSymbolLookup::find_closest(SymbolMap &map, ProcessAddress_t pc) {
  bool is_within = false;

  // First element not less than (can match exactly a start addr)
  auto it = map.lower_bound(pc);
  if (it != map.end()) { // map is empty
    is_within = symbol_is_within(pc, *it);
    if (is_within) {
      return std::make_pair<SymbolMap::iterator, bool>(std::move(it),
                                                       std::move(is_within));
    }
  }

  // previous element is more likely to contain our addr
  if (it != map.begin()) {
    --it;
  } else { // map is empty
    return std::make_pair<SymbolMap::iterator, bool>(map.end(), false);
  }
  // element can not be end (as we reversed or exit)
  is_within = symbol_is_within(pc, *it);

  return std::make_pair<SymbolMap::iterator, bool>(std::move(it),
                                                   std::move(is_within));
}

bool RuntimeSymbolLookup::symbol_is_within(ProcessAddress_t pc,
                                           const SymbolMap::value_type &kv) {
  if (pc < kv.first) {
    return false;
  }
  if (pc > kv.second.get_end()) {
    return false;
  }
  return true;
}

} // namespace ddprof