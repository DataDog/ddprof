#include "stackchecker.hpp"

#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <streambuf>
#include <string>

namespace suw {

using Symbol = ddprof::Symbol;

void to_json(json &j, const Symbol &symbol) {
  j = json{{"src_path", symbol._srcpath},
           {"demangle_name", symbol._demangle_name}};
}

void from_json(const json &j, Symbol &symbol) {
  j.at("src_path").get_to(symbol._srcpath);
  j.at("demangle_name").get_to(symbol._demangle_name);
}

void add_symbol(json &j, const Symbol &symbol) {
  json symbol_j;
  to_json(symbol_j, symbol);
  j.push_back(symbol_j);
}
static void write_json_file(std::string exe_name, const json &data,
                            std::string data_directory) {
  std::string file_path;
  if (data_directory.empty())
    file_path = std::string(STACK_DATA);
  else
    file_path = std::string(data_directory);
  file_path += "/" + std::string(exe_name) + ".json";
  std::cerr << "--> Writing json data to file: " << file_path << std::endl;
  std::ofstream file(file_path);
  if (!file) {
    throw std::runtime_error("Unable to open " + file_path + " for writing");
  }
  constexpr int k_indent_spaces = 2;
  file << data.dump(k_indent_spaces);
}

void write_json_file(std::string exe_name, const SymbolMap &map,
                     std::string data_directory) {
  json unique_symbol;
  for (const auto &info : map) {
    add_symbol(unique_symbol, info.second);
  }
  write_json_file(exe_name, unique_symbol, data_directory);
}

json parse_json_file(const std::string &filePath) {
  json ret;
  std::ifstream file(filePath);
  if (!file) {
    throw std::runtime_error("Unable to open " + filePath + " for reading");
  }
  ret = json::parse(std::string((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>()));

  return ret;
}

int compare_to_ref(std::string exe_name, const SymbolMap &map,
                   std::string data_directory) {
  std::string file_path;
  if (data_directory.empty())
    file_path = std::string(STACK_DATA);
  else
    file_path = std::string(data_directory);

  file_path += "/" + std::string(exe_name) + "_ref" + ".json";
  json ref_json = parse_json_file(file_path);
  SymbolMap ref_symbol_map;
  for (auto json_el : ref_json) {
    Symbol symbol;
    from_json(json_el, symbol);
    DwflSymbolKey key(symbol);
    ref_symbol_map[key] = symbol;
  }
  if (ref_symbol_map.empty()) {
    throw std::runtime_error("Unable to create reference set");
  }
  // Loop on all reference elements
  int cpt_not_found = 0;
  for (const auto &pair_el : ref_symbol_map) {
    if (map.find(pair_el.first) == map.end()) {
      ++cpt_not_found;
      std::cerr << "Unable to find :" << pair_el.second._demangle_name
                << std::endl;
    }
  }
  int failures = cpt_not_found * 100 / ref_symbol_map.size();
  std::cerr << "******************************" << std::endl;
  std::cerr << "Failures (%) = " << failures << std::endl;
  std::cerr << "******************************" << std::endl;
  if (failures > k_failure_threshold) {
    return 1;
  }
  return 0;
}

} // namespace suw