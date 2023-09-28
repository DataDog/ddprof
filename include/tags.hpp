// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <string>
#include <vector>

namespace ddprof {

using Tag = std::pair<std::string, std::string>;
using Tags = std::vector<Tag>;

void split(std::string_view str_view, Tags &tags, char c = ',');

struct UserTags {
  UserTags(std::string_view tag_str, int nproc);
  Tags _tags;
};

} // namespace ddprof
