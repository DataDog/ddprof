#include "ddres.h"
#include <cstddef>
#include <fstream>
#include <limits>

namespace ddprof {

DDRes sys_read_int_from_file(const char *filename, int32_t &val) {
  val = std::numeric_limits<int32_t>::max();
  std::ifstream input_file(filename);
  if (!input_file.is_open()) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_SYS_CONFIG,
                           "Unable to open system filename");
  }
  if (!(input_file >> val)) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_SYS_CONFIG, "Unable read value from file %s",
                           filename);
  }
  return ddres_init();
}

DDRes sys_perf_event_paranoid(int32_t &val) {
  val = std::numeric_limits<int32_t>::max();
  DDRES_CHECK_FWD(
      sys_read_int_from_file("/proc/sys/kernel/perf_event_paranoid", val));
  return ddres_init();
}

} // namespace ddprof
