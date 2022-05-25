// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

extern "C" {
#include "ddprof_context.h"
#include "ddprof_defs.h"
#include "ddres_def.h"
#include "perf_watcher.h"
#include "unwind_output.h"
}
#include "tags.hpp"

struct ddprof_ffi_Profile;
struct SymbolHdr;

struct DDProfPProf {
  DDProfPProf() noexcept {}
  /* single profile gathering several value types */
  ddprof_ffi_Profile *_profile = nullptr;
  unsigned _nb_values = 0;
  ddprof::Tags _tags;
};

DDRes pprof_create_profile(DDProfPProf *pprof, DDProfContext *ctx);

/**
 * Aggregate to the existing profile the provided unwinding output.
 * @param uw_output
 * @param value matching the watcher type (ex : cpu period)
 * @param watcher_idx matches the registered order at profile creation
 * @param pprof
 */
DDRes pprof_aggregate(const UnwindOutput *uw_output,
                      const SymbolHdr *symbol_hdr, uint64_t value,
                      const PerfWatcher *watcher, DDProfPProf *pprof);

DDRes pprof_reset(DDProfPProf *pprof);

DDRes pprof_write_profile(const DDProfPProf *pprof, int fd);

DDRes pprof_free_profile(DDProfPProf *pprof);
