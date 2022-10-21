#include <gtest/gtest.h>

#include "build_id.hpp"
#include "loghandle.hpp"

namespace ddprof {

// Example
// 9432ac939c015159ea375ec0a8750df908058a5a

TEST(build_id, format) {
  LogHandle handle;
  {
    const unsigned char build_id_tab[2] = {0x01, 0x01};
    BuildIdSpan build_id_span(build_id_tab);
    BuildIdStr build_id_str(format_build_id(build_id_tab));
    EXPECT_EQ(build_id_str, std::string("0101"));
    LG_DBG("format = %s", build_id_str.c_str());
  }
  {
    const unsigned char build_id_tab[] = {
        0x94, 0x32, 0xac, 0x93, 0x9c, 0x01, 0x51, 0x59, 0xea, 0x37,
        0x5e, 0xc0, 0xa8, 0x75, 0x0d, 0xf9, 0x08, 0x05, 0x8a, 0x5a};
    BuildIdSpan build_id_span(build_id_tab);
    BuildIdStr build_id_str(format_build_id(build_id_tab));
    LG_DBG("format = %s", build_id_str.c_str());
    EXPECT_EQ(build_id_str,
              std::string("9432ac939c015159ea375ec0a8750df908058a5a"));
  }
}

} // namespace ddprof
