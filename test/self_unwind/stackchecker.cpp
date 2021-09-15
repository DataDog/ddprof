#include "stackchecker.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <streambuf>
#include <string_view>

namespace suw {

using IPInfo = ddprof::IPInfo;

void to_json(json &j, const IPInfo &ip_info) {
  j = json{{"offset", ip_info._offset},
           {"demangle_name", ip_info._demangle_name}};
}

void from_json(const json &j, IPInfo &ip_info) {
  j.at("offset").get_to(ip_info._offset);
  j.at("demangle_name").get_to(ip_info._demangle_name);
}

void add_ipinfo(json &j, const IPInfo &ip_info) {
  json ipinfo_j;
  to_json(ipinfo_j, ip_info);
  j.push_back(ipinfo_j);
}
static void write_json_file(std::string_view exe_name, const json &data,
                            std::string_view data_directory) {
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

void write_json_file(std::string_view exe_name, const IPInfoMap &map,
                     std::string_view data_directory) {
  json unique_ipinfo;
  for (const auto &info : map) {
    add_ipinfo(unique_ipinfo, info.second);
  }
  write_json_file(exe_name, unique_ipinfo, data_directory);
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

int compare_to_ref(std::string_view exe_name, const IPInfoMap &map,
                   std::string_view data_directory) {
  std::string file_path;
  if (data_directory.empty())
    file_path = std::string(STACK_DATA);
  else
    file_path = std::string(data_directory);

  file_path += "/" + std::string(exe_name) + "_ref" + ".json";
  json ref_json = parse_json_file(file_path);
  IPInfoMap ref_ip_info_map;
  for (auto json_el : ref_json) {
    IPInfo ip_info;
    from_json(json_el, ip_info);
    IPInfoKey key(ip_info);
    ref_ip_info_map[key] = ip_info;
  }
  if (ref_ip_info_map.empty()) {
    throw std::runtime_error("Unable to create reference set");
  }
  // Loop on all reference elements
  int cpt_not_found = 0;
  for (const auto &pair_el : ref_ip_info_map) {
    if (map.find(pair_el.first) == map.end()) {
      ++cpt_not_found;
      std::cerr << "Unable to find :" << pair_el.second._demangle_name
                << std::endl;
    }
  }
  int failures = cpt_not_found * 100 / ref_ip_info_map.size();
  std::cerr << "******************************" << std::endl;
  std::cerr << "Failures (%) = " << failures << std::endl;
  std::cerr << "******************************" << std::endl;
  if (failures > k_failure_threshold) {
    return 1;
  }
  return 0;
}

} // namespace suw