// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <random>

#include "absl/strings/str_format.h"
#include "uuid.hpp"

namespace ddprof {
// NOLINTBEGIN(readability-magic-numbers)
Uuid::Uuid() {
  std::random_device rd;
  std::array<int32_t, 4> seed_data;
  // Generate seed data
  // as we only are using 4 integers, we avoid using the mersenne twister
  std::generate_n(seed_data.data(), seed_data.size(), std::ref(rd));

  // fill the data array with the seed data
  // we do not need to worry about distribution as we limit to 15 with the mask
  int index = 0;
  for (const auto &seed : seed_data) {
    data[index++] = (seed >> 28) & 0x0F;
    data[index++] = (seed >> 24) & 0x0F;
    data[index++] = (seed >> 20) & 0x0F;
    data[index++] = (seed >> 16) & 0x0F;
    data[index++] = (seed >> 12) & 0x0F;
    data[index++] = (seed >> 8) & 0x0F;
    data[index++] = (seed >> 4) & 0x0F;
    data[index++] = seed & 0x0F;
  }

  // Set the version to 4 (UUID version 4)
  data[k_version_position] = k_version;

  // Variant is of the form 10XX
  // So we set the first bits, then we use the random
  // This should be random from 8 to 11 (8 to b)
  data[k_variant_position] = 0x8 | (data[k_variant_position] & 0x03);
}

// we could loop instead to make things more readable,
// though that would be worse to understand the format
std::string Uuid::to_string() const {
  return absl::StrFormat("%x%x%x%x%x%x%x%x-"
                         "%x%x%x%x-"
                         "%x%x%x%x-"
                         "%x%x%x%x-"
                         "%x%x%x%x%x%x%x%x%x%x%x%x",
                         data[0], data[1], data[2], data[3], data[4], data[5],
                         data[6], data[7], data[8], data[9], data[10], data[11],
                         data[12], data[13], data[14], data[15], data[16],
                         data[17], data[18], data[19], data[20], data[21],
                         data[22], data[23], data[24], data[25], data[26],
                         data[27], data[28], data[29], data[30], data[31]);
}
// NOLINTEND(readability-magic-numbers)
} // namespace ddprof
