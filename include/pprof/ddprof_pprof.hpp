// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddog_profiling_utils.hpp"
#include "ddprof_context.hpp"
#include "ddprof_defs.hpp"
#include "ddprof_file_info.hpp"
#include "ddres_def.hpp"
#include "perf_watcher.hpp"
#include "symbolizer.hpp"
#include "tags.hpp"
#include "unwind_output.hpp"

struct blaze_symbolizer;

namespace ddprof {

struct Symbolizer;
struct SymbolHdr;

struct DDProfPProf {
  /* single profile gathering several value types */
  ddog_prof_Profile _profile{};
  unsigned _nb_values = 0;
  Tags _tags;
  bool use_process_adresses{true};
};

struct DDProfValuePack {
  int64_t value;
  uint64_t count;
  uint64_t timestamp;
};

DDRes pprof_create_profile(DDProfPProf *pprof, DDProfContext &ctx);

/**
 * Aggregate to the existing profile the provided unwinding output.
 * @param uw_output
 * @param pack combines the value, count, and timestamp of an event
 * @param watcher_idx matches the registered order at profile creation
 * @param pprof
 */
DDRes pprof_aggregate(const UnwindOutput *uw_output,
                      const SymbolHdr &symbol_hdr,
                      ddprof::Symbolizer *symbolizer,
                      const DDProfValuePack &pack, const PerfWatcher *watcher,
                      const FileInfoVector &vector,
                      EventAggregationModePos value_pos, DDProfPProf *pprof);

DDRes pprof_reset(DDProfPProf *pprof);

DDRes pprof_write_profile(const DDProfPProf *pprof, int fd);

DDRes pprof_free_profile(DDProfPProf *pprof);

void ddprof_print_sample(const UnwindOutput &uw_output,
                         const SymbolHdr &symbol_hdr, uint64_t value,
                         EventAggregationModePos value_mode_pos,
                         const PerfWatcher &watcher);

} // namespace ddprof
