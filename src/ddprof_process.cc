#include "ddprof_process.hpp"

#include "ddres.hpp"
#include "defer.hpp"

#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <string.h> // for strerror()
#include <unistd.h>
#include <vector>

namespace ddprof {
constexpr auto k_max_buf_cgroup_link = 1024;

DDRes CGroup::read_id(pid_t pid) {
  std::string path = "/proc/" + std::to_string(pid) + "/ns/cgroup";
  char buf[k_max_buf_cgroup_link];
  ssize_t len = readlink(path.c_str(), buf, k_max_buf_cgroup_link - 1);

  if (len == -1) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_CGROUP, "Unable to read link %s",
                           path.c_str());
  }

  buf[len] = '\0'; // null terminate the string

  std::string_view linkTarget(buf);
  size_t start = linkTarget.find_last_of('[');
  size_t end = linkTarget.find_last_of(']');

  if (start == std::string::npos || end == std::string::npos) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_CGROUP, "Unable to find id %s",
                           linkTarget.data());
  }

  std::string_view id_str = linkTarget.substr(start + 1, end - start - 1);

  _id = std::stoull(std::string(id_str));
  return {};
}
} // namespace ddprof
