#pragma once
#include "unwind_output.h"

#define K_MOCK_LOC_SIZE 11
static const char *s_func_names[K_MOCK_LOC_SIZE] = {
    "foo0", "foo1", "foo2", "foo3", "foo4", "foo5",
    "foo6", "foo7", "foo8", "foo9", "foo10"};
static const char *s_src_paths[K_MOCK_LOC_SIZE] = {
    "/app/0/bar.c", "/app/1/bar.c", "/app/2/bar.c", "/app/3/bar.c",
    "/app/4/bar.c", "/app/5/bar.c", "/app/6/bar.c", "/app/7/bar.c",
    "/app/8/bar.c", "/app/9/bar.c", "/app/10/bar.c"};

static const char *s_so_paths[] = {"/app/lib/bar.0.so"};

// ddprof_ffi_Mapping

static inline void fill_unwind_output_1(UnwindOutput *uw_output) {

  uw_output_clear(uw_output);
  uw_output->nb_locs = K_MOCK_LOC_SIZE;

  FunLoc *locs = uw_output->locs;
  for (unsigned i = 0; i < uw_output->nb_locs; ++i) {
    locs[i].funname = string_view_create_strlen(s_func_names[i]);
    locs[i].srcpath = string_view_create_strlen(s_src_paths[i]);
    locs[i].sopath = string_view_create_strlen(s_so_paths[0]);
    locs[i].ip = 42 + i;
    locs[i].map_start = 100 + i;
    locs[i].map_end = 200 + i;
    locs[i].map_off = 300 + i;
    locs[i].line = 10 * i;
  }
}
