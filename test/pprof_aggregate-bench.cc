// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// Micro-benchmark for the pprof sample-aggregation hot path.
// Mirrors TEST(DDProfPProf, aggregate) in ddprof_pprof-ut.cc but runs the
// aggregation in a tight loop so we can measure ns/sample.

#include <benchmark/benchmark.h>

#include "ddog_profiling_utils.hpp"
#include "ddprof_cmdline.hpp"
#include "ddprof_cmdline_watcher.hpp"
#include "loghandle.hpp"
#include "pevent_lib_mocks.hpp"
#include "pprof/ddprof_pprof.hpp"
#include "symbol_hdr.hpp"
#include "unwind_output_mock.hpp"

namespace ddprof {

static void BM_PProfAggregateInternedSample(benchmark::State &state) {
  LogHandle handle;
  SymbolHdr symbol_hdr;
  UnwindOutput mock_output;
  SymbolTable &table = symbol_hdr._symbol_table;
  MapInfoTable &mapinfo_table = symbol_hdr._mapinfo_table;
  FileInfoVector file_infos;
  fill_unwind_symbols(table, mapinfo_table, mock_output,
                      symbol_hdr.profiles_dictionary());

  DDProfPProf pprof;
  DDProfContext ctx = {};
  (void)watchers_from_str("sCPU", ctx.watchers);
  (void)pprof_create_profile(&pprof, ctx,
                             symbol_hdr._profiles_dictionary.get());

  for (auto _ : state) {
    DDRes res = pprof_aggregate_interned_sample(
        &mock_output, symbol_hdr, {1000, 1, 0}, &ctx.watchers[0], file_infos,
        false, kSumPos, ctx.worker_ctx.symbolizer, &pprof);
    benchmark::DoNotOptimize(res);
  }

  (void)pprof_free_profile(&pprof);
}

BENCHMARK(BM_PProfAggregateInternedSample);

} // namespace ddprof
