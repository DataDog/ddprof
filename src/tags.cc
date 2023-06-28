// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "tags.hpp"

#include "logger.hpp"
#include "thread_info.hpp"

#include <utility>

namespace {
// From the DogFood repo. Credit goes to Garrett Sickles, who has an awesome
// DogStatsD C++ library : https://github.com/garrettsickles/DogFood.git
inline bool ValidateTags(std::string_view tag) {
  if (tag.length() == 0 || tag.length() > 200)
    return false;

  ////////////////////////////////////////////////////////
  // Verify the first character is a letter
  if (!std::isalpha(tag.at(0)))
    return false;

  ////////////////////////////////////////////////////////
  // Verify end is not a colon
  if (tag.back() == ':')
    return false;

  ////////////////////////////////////////////////////////
  // Verify each character
  for (size_t n = 0; n < tag.length(); n++) {
    const char c = tag.at(n);
    if (std::isalnum(c) || c == '_' || c == '-' || c == ':' || c == '.' ||
        c == '/' || c == '\\')
      continue;
    else
      return false;
  }
  return true;
}

} // namespace
namespace ddprof {

static Tag split_kv(std::string_view str_view, char c = ':') {
  size_t pos = str_view.find(c);

  // Check if no ':' character was found or if it's the last character
  if (pos == std::string_view::npos || pos == str_view.size() - 1) {
    LG_WRN("[TAGS] Error, bad tag value %s", str_view.data());
    return Tag();
  }

  return {std::string(str_view.substr(0, pos)),
          std::string(str_view.substr(pos + 1))};
}

void split(std::string_view str_view, Tags &tags, char c) {
  size_t begin = 0;

  while (begin < str_view.size()) {
    size_t end = str_view.find(c, begin);
    if (end == std::string_view::npos) {
      end = str_view.size();
    }

    std::string_view tag = str_view.substr(begin, end - begin);

    if (!ValidateTags(tag)) {
      LG_WRN("[TAGS] Bad tag value - skip %s", tag.data());
      begin = end + 1;
      continue;
    }

    Tag current_tag = split_kv(tag);
    if (current_tag == Tag()) {
      // skip this empty tag
      begin = end + 1;
      continue;
    }

    tags.push_back(std::move(current_tag));
    begin = end + 1;
  }
}

} // namespace ddprof

UserTags::UserTags(std::string_view tag_str, int nproc) {
  if (!tag_str.empty()) {
    ddprof::split(tag_str, _tags);
  }
  _tags.push_back({"number_of_cpu_cores", std::to_string(nproc)});
  _tags.push_back({"number_hw_concurent_threads",
                   std::to_string(ddprof::get_nb_hw_thread())});
}
