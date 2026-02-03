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
#include "tags.hpp"
#include "unwind_output.hpp"

#include <unordered_map>

namespace ddprof {

class Symbolizer;
struct SymbolHdr;

struct DDProfPProf {
  struct LabelKeyIds {
    ddog_prof_StringId container_id{};
    ddog_prof_StringId process_id{};
    ddog_prof_StringId process_name{};
    ddog_prof_StringId thread_id{};
    ddog_prof_StringId thread_name{};
    ddog_prof_StringId tracepoint_type{};
    bool initialized{false};
  };

  struct LabelKeyPair {
    ddog_prof_StringId key{};
    ddog_prof_StringId value{};
  };

  struct LabelKeyPairHash {
    size_t operator()(const LabelKeyPair &pair) const noexcept;
  };

  /* single profile gathering several value types */
  ddog_prof_Profile _profile{};
  unsigned _nb_values = 0;
  Tags _tags;
  // avoid re-creating strings for all pid numbers
  std::unordered_map<pid_t, std::string> _pid_str;
  std::unordered_map<pid_t, ddog_prof_StringId> _pid_string_ids;
  HeterogeneousLookupStringMap<ddog_prof_StringId> _label_value_ids;
  std::unordered_map<LabelKeyPair, ddog_prof_LabelId, LabelKeyPairHash>
      _label_id_cache;
  LabelKeyIds _label_key_ids;
};

inline bool operator==(const DDProfPProf::LabelKeyPair &lhs,
                       const DDProfPProf::LabelKeyPair &rhs) {
  return lhs.key.generation.id == rhs.key.generation.id &&
         lhs.key.id.offset == rhs.key.id.offset &&
         lhs.value.generation.id == rhs.value.generation.id &&
         lhs.value.id.offset == rhs.value.id.offset;
}

struct DDProfValuePack {
  int64_t value;
  uint64_t count;
  uint64_t timestamp;
};

DDRes pprof_create_profile(DDProfPProf *pprof, DDProfContext &ctx,
                           const ddog_prof_ProfilesDictionaryHandle *dict);

/**
 * Aggregate to the existing profile the provided unwinding output.
 * @param uw_output
 * @param pack combines the value, count, and timestamp of an event
 * @param watcher_idx matches the registered order at profile creation
 * @param pprof
 */
DDRes pprof_aggregate_interned_sample(
    const UnwindOutput *uw_output, const SymbolHdr &symbol_hdr,
    const DDProfValuePack &pack, const PerfWatcher *watcher,
    const FileInfoVector &file_infos, bool show_samples,
    EventAggregationModePos value_pos, Symbolizer *symbolizer,
    DDProfPProf *pprof);

DDRes pprof_reset(DDProfPProf *pprof);

DDRes pprof_write_profile(const DDProfPProf *pprof, int fd);

DDRes pprof_free_profile(DDProfPProf *pprof);

} // namespace ddprof
