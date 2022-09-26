// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "tempfile.hpp"

#include "ddres_helpers.hpp"
#include "defer.hpp"

#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace ddprof {

DDRes create_temp_file(std::string_view prefix,
                       ddprof::span<const std::byte> data, mode_t mode,
                       std::string &path) {
  auto template_str =
      std::string{std::filesystem::temp_directory_path() / prefix} + ".XXXXXX";

  // Create temporary file
  int fd = mkostemp(template_str.data(), O_CLOEXEC);
  DDRES_CHECK_ERRNO(fd, DD_WHAT_TEMP_FILE, "Failed to create temporary file");

  defer { close(fd); };
  auto defer_unlink = make_defer([&] { unlink(template_str.c_str()); });

  DDRES_CHECK_ERRNO(fchmod(fd, mode), DD_WHAT_TEMP_FILE,
                    "Failed to change temp file %s permissions",
                    template_str.c_str());

  // Write embedded lib into temp file
  if (write(fd, data.data(), data.size()) !=
      static_cast<ssize_t>(data.size())) {
    DDRES_CHECK_ERRNO(-1, DD_WHAT_TEMP_FILE, "Failed to write temporary file");
  }

  path = std::move(template_str);
  defer_unlink.release();
  return {};
}

} // namespace ddprof
