// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "unwind.hpp"

#include "ddprof_stats.hpp"
#include "ddres.hpp"
#include "logger.hpp"
#include "signal_helper.hpp"
#include "unwind_helpers.hpp"
#include "unwind_metrics.hpp"
#include "unwind_state.hpp"

#include <algorithm>
#include <array>
#include <string_view.hpp>

#include "stackWalker.h"
#include "stack_context.h"
#include "symbols.h"

using namespace std::string_view_literals;

namespace ddprof {

void unwind_init_sample(UnwindState *us, uint64_t *sample_regs,
                        pid_t sample_pid, uint64_t sample_size_stack,
                        char *sample_data_stack) {
  us->output.nb_locs = 0;
  memcpy(&us->initial_regs.regs[0], sample_regs,
         K_NB_REGS_UNWIND * sizeof(uint64_t));
  us->current_ip = us->initial_regs.regs[REGNAME(PC)];
  us->pid = sample_pid;
  us->stack_sz = sample_size_stack;
  us->stack = sample_data_stack;
}

static bool is_ld(const std::string &path) {
  // path is expected to not contain slashes
  assert(path.rfind('/') == std::string::npos);

  return path.starts_with("ld-");
}

DDRes unwindstate__unwind(UnwindState *us) {
  DDRes res = ddres_init();
  if (us->pid != 0) { // we can not unwind pid 0
    CodeCacheArray &code_cache_array = us->code_cache[us->pid];
    if (!code_cache_array.count()) {
      // No libraries
      Symbols::parsePidLibraries(us->pid, &us->code_cache[us->pid], false);
      // todo how do we avoid bouncing on this ?
    }

    ddprof::span<uint64_t, PERF_REGS_COUNT> regs_span{us->initial_regs.regs,
                                                      PERF_REGS_COUNT};
    ap::StackContext sc = ap::from_regs(regs_span);
    ddprof::span<std::byte> stack{reinterpret_cast<std::byte *>(us->stack),
                                  us->stack_sz};
    ap::StackBuffer buffer(stack, sc.sp, sc.sp + us->stack_sz);

    // todo remove char* in favour of uint64
    us->output.nb_locs =
        stackWalk(&code_cache_array, sc, buffer, (us->output.callchain),
                  DD_MAX_STACK_DEPTH, 0);
  }
  // todo error management (error frame)

  // Add a frame that identifies executable to which these belong
  // todo base frame

  return res;
}

void unwind_pid_free(UnwindState *us, pid_t pid) { us->code_cache.erase(pid); }

void unwind_cycle(UnwindState *) { unwind_metrics_reset(); }

} // namespace ddprof
