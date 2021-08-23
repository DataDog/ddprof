#include "stdlib.h"
#include "string.h"

#include "ddprof/dd_send.h"
#include "ddprof_export.h"
#include "ddprof_stats.h"
#include "ddres.h"
#include "procutils.h"

void print_diagnostics() {
#ifdef DBG_JEMALLOC
  // jemalloc stats
  malloc_stats_print(NULL, NULL, "");
#endif
}

DDRes export(DDProfContext *ctx, int64_t now) {
  DDReq *ddr = ctx->ddr;
  DProf *dp = ctx->dp;

  // Before any state gets reset, export metrics to statsd
  // TODO actually do that

  // And emit diagnostic output (if it's enabled)
  print_diagnostics();

  LG_NTC("Pushing samples to backend");
  int ret = 0;
  if ((ret = DDR_pprof(ddr, dp)))
    LG_ERR("Error enqueuing pprof (%s)", DDR_code2str(ret));
  DDR_setTimeNano(ddr, dp->pprof.time_nanos, now);
  if ((ret = DDR_finalize(ddr)))
    LG_ERR("Error finalizing export (%s)", DDR_code2str(ret));
  if ((ret = DDR_send(ddr)))
    LG_ERR("Error sending export (%s)", DDR_code2str(ret));
  if ((ret = DDR_watch(ddr, -1)))
    LG_ERR("Error watching (%d : %s)", ddr->res.code, DDR_code2str(ret));
  DDR_clear(ddr);

  // Update the time last sent
  ctx->send_nanos += ctx->params.upload_period * 1000000000;

  // Prepare pprof for next window
  pprof_timeUpdate(dp);

  // Increase the counts of exports
  ctx->count_worker += 1;
  ctx->count_cache += 1;

  // We're done exporting, so finish by clearing out any global gauges
  ddprof_stats_clear(STATS_UNWIND_TICKS);
  ddprof_stats_clear(STATS_EVENT_LOST);
  ddprof_stats_clear(STATS_SAMPLE_COUNT);

  return ddres_init();
}
