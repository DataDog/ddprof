#include "ddres_def.h"
#include <string_view>

namespace ddprof {

DDRes sys_perf_event_paranoid(int32_t &val);

DDRes sys_read_int_from_file(const char *filename, int32_t &val);
} // namespace ddprof
