// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_module_lib.hpp"

#include "build_id.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "logger.hpp"
#include "unique_fd.hpp"

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <unistd.h>

namespace ddprof {

std::vector<Mapping> get_mappings(const DsoHdr::DsoConstRange &dso_range,
                                  bool keep_only_executable_mappings) {
  std::vector<Mapping> mappings;

  for (auto it = dso_range.first; it != dso_range.second; ++it) {
    const Dso &mapping = it->second;
    if (!keep_only_executable_mappings || mapping.is_executable()) {
      mappings.push_back(Mapping{.addr = mapping._start,
                                 .offset = mapping._offset,
                                 .prot = mapping._prot});
    }
  }
  return mappings;
}

DDRes get_elf_load_segments(int fd, const std::string &filepath,
                            bool keep_only_executable_segments,
                            std::vector<Segment> &segments) {
  Elf *elf = elf_begin(fd, ELF_C_READ_MMAP, nullptr);
  if (elf == nullptr) {
    LG_WRN("Invalid elf %s", filepath.c_str());
    return ddres_error(DD_WHAT_INVALID_ELF);
  }
  defer { elf_end(elf); };
  GElf_Ehdr ehdr_mem;
  GElf_Ehdr *ehdr = gelf_getehdr(elf, &ehdr_mem);
  if (ehdr == nullptr) {
    LG_WRN("Invalid elf %s", filepath.c_str());
    return ddres_error(DD_WHAT_INVALID_ELF);
  }

  switch (ehdr->e_type) {
  case ET_EXEC:
  case ET_CORE:
  case ET_DYN: {
    size_t phnum;
    if (unlikely(elf_getphdrnum(elf, &phnum) != 0)) {
      LG_WRN("Invalid elf %s", filepath.c_str());
      return ddres_error(DD_WHAT_INVALID_ELF);
    }
    for (size_t i = 0; i < phnum; ++i) {
      GElf_Phdr phdr_mem;
      GElf_Phdr *ph = gelf_getphdr(elf, i, &phdr_mem);
      if (unlikely(ph == nullptr)) {
        LG_WRN("Invalid elf %s", filepath.c_str());
        return ddres_error(DD_WHAT_INVALID_ELF);
      }
      if (ph->p_type == PT_LOAD &&
          (!keep_only_executable_segments || (ph->p_flags & PF_X))) {
        segments.push_back(Segment{ph->p_vaddr, ph->p_offset, ph->p_align,
                                   elf_flags_to_prot(ph->p_flags)});
      }
    }
    break;
  }
  default:
    LG_WRN("Unsupported elf type (%d) %s", ehdr->e_type, filepath.c_str());
    return ddres_error(DD_WHAT_INVALID_ELF);
  }

  return {};
}

namespace {
/** Find segment that matches mapping among segments.
 * If several segments match, return {firs_matching_segment, true}
 * If no segment matches, return {nullptr, false}
 * If one segment matches, return {segment, false}
 */
std::pair<const Segment *, bool>
find_matching_segment(std::span<const Segment> segments,
                      const Mapping &mapping) {
  const Segment *matching_segment = nullptr;

  for (const Segment &segment : segments) {
    const auto aligned_offset = segment.offset & ~(segment.alignment - 1);
    if (mapping.prot == segment.prot && mapping.offset == aligned_offset) {
      if (matching_segment) {
        // multiple matching segments
        return {matching_segment, true};
      }
      matching_segment = &segment;
    }
  }
  return {matching_segment, false};
}

DDRes compute_elf_bias(int fd, const std::string &filepath,
                       const DsoHdr::DsoConstRange &dso_range, Offset_t &bias) {
  // To compute elf bias, we need to match a Dso (ie. a mapping in process
  // address space) with the corresponding LOAD segment in elf file. We consider
  // only executable mappings LOAD segments (because we configure perf in such
  // a way that we receive only executable mmap events).
  // The matching is done by comparing mapping offset with aligned file offset
  // (file offset may not be aligned, in particular with lld, but
  // when segment is mmapped, mmap offset needs to be page aligned, file offset
  // is rounded down to satisfy segment p_align alignment) and memory
  // protection of segment.
  // Several segments can have the same aligned offsets and protections though,
  // thus making the matching ambiguous.
  // Any mapping can be used for matching, but some mappings might be missing
  // because of lost perf events. We start with the last mapping in address
  // order, because it is the one that has the most constraints: if we observed
  // n executable mappings, then we know that the last mapping is preceded by at
  // least n-1 other exec mappings and therefore cannot match the first n-1 LOAD
  // exec segments. If last mapping leads to an ambiguous match, we try again
  // with the second to last mapping and so on...

  if (dso_range.first == dso_range.second) {
    LG_WRN("Empty mappings for %s", filepath.c_str());
    return ddres_error(DD_WHAT_NO_MATCHING_LOAD_SEGMENT);
  }

  bool const useOnlyExecSegments =
      (dso_range.first->second._origin == DsoOrigin::kPerfMmapEvent);

  // todo: use small/inlined vector
  std::vector<Segment> elf_segments;

  DDRES_CHECK_FWD_STRICT(
      get_elf_load_segments(fd, filepath, useOnlyExecSegments, elf_segments));

  auto executable_mappings = get_mappings(dso_range, useOnlyExecSegments);
  MatchResult const match_result =
      find_match(executable_mappings, elf_segments, useOnlyExecSegments);

  if (match_result.is_ambiguous) {
    LG_WRN("Multiple matching segments: %s", filepath.c_str());
    return ddres_error(DD_WHAT_AMBIGUOUS_LOAD_SEGMENT);
  }
  if (!match_result.load_segment || !match_result.mapping) {
    LG_WRN("Not matching LOAD segment found in %s", filepath.c_str());
    return ddres_error(DD_WHAT_NO_MATCHING_LOAD_SEGMENT);
  }

  const auto &mapping = *match_result.mapping;
  const auto &segment = *match_result.load_segment;
  bias = mapping.addr - mapping.offset - (segment.addr - segment.offset);

  return {};
}
} // namespace

MatchResult find_match(std::span<const Mapping> executable_mappings,
                       std::span<const Segment> elf_load_segments,
                       bool possible_missing_mappings) {

  if (!possible_missing_mappings && !executable_mappings.empty() &&
      !elf_load_segments.empty()) {
    // Easy case: all mappings are present, just use the first LOAD segment
    return {executable_mappings.data(), elf_load_segments.data(), false};
  }

  // In some weird cases, number of mappings is bigger than number of segments
  // (eg. with dotnet libcoreclr.so) because code changes the permissions of
  // some memory ranges inside the mappings, hence splitting the mapping into
  // several parts.
  // For now just cap the number of considered mappings with the number of
  // segments.
  int const nb_mappings =
      std::min(executable_mappings.size(), elf_load_segments.size());

  MatchResult res = {nullptr, nullptr, false};
  for (int mapping_idx = nb_mappings - 1; mapping_idx >= 0; --mapping_idx) {
    const auto &mapping = executable_mappings[mapping_idx];
    assert(static_cast<unsigned>(mapping_idx) < elf_load_segments.size());
    auto [matching_segment, ambiguous] = find_matching_segment(
        std::span{elf_load_segments}.subspan(mapping_idx), mapping);

    if (matching_segment) {
      res.load_segment = matching_segment;
      res.mapping = &mapping;
    }
    if (!ambiguous) {
      return res;
    }
    res.is_ambiguous = true;
  }

  return res;
}

DDRes report_module(Dwfl *dwfl, ProcessAddress_t pc,
                    const DsoHdr::DsoConstRange &dsoRange,
                    const FileInfoValue &fileInfoValue, DDProfMod &ddprof_mod) {
  const std::string &filepath = fileInfoValue.get_path();
  const char *module_name = strrchr(filepath.c_str(), '/') + 1;
  if (fileInfoValue._errored) { // avoid bouncing on errors
    LG_DBG("DSO Previously errored - mod (%s)", module_name);
    return ddres_warn(DD_WHAT_MODULE);
  }

  Dwfl_Module *mod = dwfl_addrmodule(dwfl, pc);

  if (mod) {
    // There should not be a module already loaded at this address
    const char *main_name = nullptr;
    Dwarf_Addr low_addr;
    Dwarf_Addr high_addr;
    dwfl_module_info(mod, nullptr, &low_addr, &high_addr, nullptr, nullptr,
                     &main_name, nullptr);
    LG_NTC("Incoherent modules[PID=%d]: module %s [%lx-%lx] is already "
           "loaded at %lx(%s[ID#%d])",
           dsoRange.first->second._pid, main_name, low_addr, high_addr, pc,
           filepath.c_str(), fileInfoValue.get_id());
    ddprof_mod._status = DDProfMod::kInconsistent;
    return ddres_warn(DD_WHAT_MODULE);
  }

  UniqueFd fd_holder{::open(filepath.c_str(), O_RDONLY)};
  if (!fd_holder) {
    LG_WRN("[Mod] Couldn't open fd to module (%s)", filepath.c_str());
    return ddres_warn(DD_WHAT_MODULE);
  }
  LG_DBG("[Mod] Success opening %s, ", filepath.c_str());

  // Load the file at a matching DSO address
  dwfl_errno(); // erase previous error
  Offset_t bias = 0;
  auto res = compute_elf_bias(fd_holder.get(), filepath, dsoRange, bias);
  if (!IsDDResOK(res)) {
    fileInfoValue._errored = true;
    LG_WRN("Couldn't retrieve offsets from %s(%s)", module_name,
           fileInfoValue.get_path().c_str());
    return res;
  }

  ddprof_mod._mod = dwfl_report_elf(dwfl, module_name, filepath.c_str(),
                                    fd_holder.get(), bias, true);

  // Retrieve build id
  const unsigned char *bits = nullptr;
  GElf_Addr vaddr;
  if (int const size = dwfl_module_build_id(ddprof_mod._mod, &bits, &vaddr);
      size > 0) {
    // ensure we called dwfl_module_getelf first (or this can fail)
    // returns the size
    ddprof_mod.set_build_id(BuildIdSpan{bits, static_cast<unsigned>(size)});
  }

  if (!ddprof_mod._mod) {
    // Ideally we would differentiate pid errors from file errors.
    // For perf reasons we will just flag the file as errored
    fileInfoValue._errored = true;
    LG_WRN("Couldn't addrmodule (%s)[0x%lx], MOD:%s (%s)", dwfl_errmsg(-1), pc,
           module_name, fileInfoValue.get_path().c_str());
    return ddres_warn(DD_WHAT_MODULE);
  }
  // dwfl now has ownership of the file descriptor
  (void)fd_holder.release();
  dwfl_module_info(ddprof_mod._mod, nullptr, &ddprof_mod._low_addr,
                   &ddprof_mod._high_addr, nullptr, nullptr, nullptr, nullptr);
  LG_DBG("Loaded mod from file (%s[ID#%d]), (%s) mod[%lx-%lx] bias[%lx], "
         "build-id: %s",
         fileInfoValue.get_path().c_str(), fileInfoValue.get_id(),
         dwfl_errmsg(-1), ddprof_mod._low_addr, ddprof_mod._high_addr, bias,
         ddprof_mod._build_id.c_str());

  ddprof_mod._sym_bias = bias;
  return {};
}

} // namespace ddprof
