#include "ddprof_process.hpp"

#include "ddres.hpp"
#include "defer.hpp"
#include "string_format.hpp"

<<<<<<< HEAD
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <string.h> // for strerror()
=======
#include <charconv> // for std::from_chars
#include <fcntl.h>
#include <iostream>
#include <sstream>
>>>>>>> c6ccb53 (Container ID lookup - WIP)
#include <unistd.h>
#include <vector>

namespace ddprof {
constexpr auto k_max_buf_cgroup_link = 1024;

DDRes Process::read_cgroup_id(pid_t pid, std::string_view path_to_proc,
                              CGroupId_t &cgroup) {
  std::string path =
      string_format("%s/proc/%d/ns/cgroup", path_to_proc.data(), pid);
  char buf[k_max_buf_cgroup_link];
  ssize_t len = readlink(path.c_str(), buf, k_max_buf_cgroup_link - 1);

  if (len == -1) {
<<<<<<< HEAD
    DDRES_RETURN_ERROR_LOG(DD_WHAT_CGROUP, "Unable to read link %s",
                           path.c_str());
=======
    DDRES_RETURN_WARN_LOG(DD_WHAT_CGROUP, "Unable to read link %s",
                          path.c_str());
>>>>>>> c6ccb53 (Container ID lookup - WIP)
  }

  buf[len] = '\0'; // null terminate the string

  std::string_view linkTarget(buf);
  size_t start = linkTarget.find_last_of('[');
  size_t end = linkTarget.find_last_of(']');

  if (start == std::string::npos || end == std::string::npos) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_CGROUP, "Unable to find id %s",
                          linkTarget.data());
  }

  std::string_view id_str = linkTarget.substr(start + 1, end - start - 1);

  auto [p, ec] =
      std::from_chars(id_str.data(), id_str.data() + id_str.size(), cgroup);
  if (ec == std::errc::invalid_argument ||
      ec == std::errc::result_out_of_range) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_CGROUP, "Unable to cgroup to number id %d",
                          pid);
  }
  return {};
}
} // namespace ddprof
