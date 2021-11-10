// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "tags.hpp"
#include "thread_info.hpp"

#include "DogFood.hpp"

extern "C" {
#include "logger.h"
}
#include <utility>

namespace ddprof {

static Tag split_kv(const char *str, size_t end_pos, char c = ':') {
  const char *begin = str;
  const char *current_str = str;
  while (*current_str != c && *current_str != '\0')
    current_str++;
  if (*current_str == '\0' || str + end_pos + 1 <= current_str) {
    LG_WRN("[TAGS] Error, bad tag value %s", str);
    return Tag();
  }
  return std::make_pair(
      std::string(begin, current_str - begin),
      std::string(current_str + 1, end_pos - (current_str - begin + 1)));
}

void split(const char *str, Tags &tags, char c) {
  if (!str) {
    // nothing to do
    return;
  }
  do {
    const char *begin = str;
    // find the comma to split a tag
    while (*str != c && *str != '\0')
      str++;
    // no need to +1 with size as we don't include the comma
    Tag current_tag = split_kv(begin, str - begin);
    if (current_tag == Tag()) {
      // skip this empty tag
      continue;
    }
    if (!DogFood::ValidateTags(current_tag.first) ||
        !DogFood::ValidateTags(current_tag.second)) {
      LG_WRN("[TAGS] Bad tag value - skip %s:%s", current_tag.first.c_str(),
             current_tag.second.c_str());
    } else {
      tags.push_back(std::move(current_tag));
    }
  } while ('\0' != *str++);
}

} // namespace ddprof

UserTags::UserTags(const char *tag_str, int nproc) {
  ddprof::split(tag_str, _tags);
  _tags.push_back(std::make_pair(std::string("number_of_cpu_cores"),
                                 std::to_string(nproc)));
  _tags.push_back(std::make_pair(std::string("number_hw_concurent_threads"),
                                 std::to_string(ddprof::get_nb_hw_thread())));
}
