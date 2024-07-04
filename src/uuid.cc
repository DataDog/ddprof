// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <random>
#include <sstream>
#include <vector>

#include "uuid.hpp"

namespace dd {

// Copied from dd-trace-dotnet, and previously
// copied from https://stackoverflow.com/a/60198074
// We replace std::mt19937 by std::mt19937_64 so we can generate 64bits numbers instead of 32bits
std::string GenerateUuidV4()
{
  static std::random_device rd;
  static std::mt19937_64 gen(rd());
  static std::uniform_int_distribution<> dis(0, 15);
  static std::uniform_int_distribution<> dis2(8, 11);

  std::stringstream ss;
  ss << std::hex;
  for (auto i = 0; i < 8; i++)
  {
    ss << dis(gen);
  }
  ss << "-";
  for (auto i = 0; i < 4; i++)
  {
    ss << dis(gen);
  }
  ss << "-4"; // according to the RFC, '4' is the 4 version
  for (auto i = 0; i < 3; i++)
  {
    ss << dis(gen);
  }
  ss << "-";
  ss << dis2(gen);
  for (auto i = 0; i < 3; i++)
  {
    ss << dis(gen);
  }
  ss << "-";
  for (auto i = 0; i < 12; i++)
  {
    ss << dis(gen);
  }
  return ss.str();
}
}
