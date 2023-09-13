// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "tempfile.hpp"

#include "ddres_helpers.hpp"
#include "defer.hpp"
#include "sha1.h"

#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace ddprof {

DDRes get_or_create_temp_file(std::string_view prefix,
                              std::span<const std::byte> data, mode_t mode,
                              std::string &path) {
  unsigned char digest[20];
  char str_digest[41];
  SHA1(digest, reinterpret_cast<const char *>(data.data()), data.size());
  SHA1StrDigest(digest, str_digest);

  std::error_code ec;
  path = std::string{std::filesystem::temp_directory_path(ec) / prefix} + "-" +
      std::string{str_digest};
  DDRES_CHECK_ERRORCODE(ec, DD_WHAT_TEMP_FILE,
                        "Failed to determine temp directory path");

  if (std::filesystem::exists(path, ec)) {
    return {};
  }
  std::string tmp_path;
  DDRES_CHECK_FWD(create_temp_file(prefix, data, mode, tmp_path));
  std::filesystem::rename(tmp_path, path, ec);
  if (ec) {
    std::filesystem::remove(tmp_path, ec);
    DDRES_RETURN_ERROR_LOG(DD_WHAT_TEMP_FILE, "Failed to rename temp file");
  }

  return {};
}

DDRes create_temp_file(std::string_view prefix, std::span<const std::byte> data,
                       mode_t mode, std::string &path) {
  std::error_code ec;
  auto template_str =
      std::string{std::filesystem::temp_directory_path(ec) / prefix} +
      ".XXXXXX";
  DDRES_CHECK_ERRORCODE(ec, DD_WHAT_TEMP_FILE,
                        "Failed to determine temp directory path");

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
