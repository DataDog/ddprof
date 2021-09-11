#pragma once

#include "ipinfo_table.hpp"
#include "mapinfo_table.hpp"
#include "unwind_output.h"

namespace ddprof {
#define K_MOCK_LOC_SIZE 11
static const char *s_syn_names[K_MOCK_LOC_SIZE] = {
    "xd_foo0", "xd_foo1", "xd_foo2", "xd_foo3", "xd_foo4", "xd_foo5",
    "xd_foo6", "xd_foo7", "xd_foo8", "xd_foo9", "xd_foo10"};

static const char *s_func_names[K_MOCK_LOC_SIZE] = {
    "foo0", "foo1", "foo2", "foo3", "foo4", "foo5",
    "foo6", "foo7", "foo8", "foo9", "foo10"};
static const char *s_src_paths[K_MOCK_LOC_SIZE] = {
    "/app/0/bar.c", "/app/1/bar.c", "/app/2/bar.c", "/app/3/bar.c",
    "/app/4/bar.c", "/app/5/bar.c", "/app/6/bar.c", "/app/7/bar.c",
    "/app/8/bar.c", "/app/9/bar.c", "/app/10/bar.c"};

static const char *s_so_paths[] = {"/app/lib/bar.0.so"};

// ddprof_ffi_Mapping

static inline void fill_ipinfo_table_1(IPInfoTable &ipinfo_table) {
  for (unsigned i = 0; i < K_MOCK_LOC_SIZE; ++i) {
    ipinfo_table.emplace_back(300 + i, std::string(s_syn_names[i]),
                              std::string(s_func_names[i]), 10 * i,
                              std::string(s_src_paths[i]));
  }
}

static inline void fill_mapinfo_table_1(MapInfoTable &mapinfo_table) {
  for (unsigned i = 0; i < K_MOCK_LOC_SIZE; ++i) {
    mapinfo_table.emplace_back(100 + i, 200 + i, std::string(s_so_paths[0]));
  }
}

static inline void fill_unwind_output_1(UnwindOutput &uw_output) {
  uw_output_clear(&uw_output);
  uw_output.nb_locs = K_MOCK_LOC_SIZE;

  FunLoc *locs = uw_output.locs;
  for (unsigned i = 0; i < uw_output.nb_locs; ++i) {
    locs[i].ip = 42 + i;
    locs[i]._ipinfo_idx = i;
    locs[i]._map_info_idx = i;
  }
}

static inline void fill_unwind_symbols(IPInfoTable &ipinfo_table,
                                       MapInfoTable &mapinfo_table,
                                       UnwindOutput &uw_output) {
  fill_ipinfo_table_1(ipinfo_table);
  fill_mapinfo_table_1(mapinfo_table);
  fill_unwind_output_1(uw_output);
}

} // namespace ddprof
