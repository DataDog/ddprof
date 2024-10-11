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

using namespace std::literals;

namespace ddprof {

namespace {

constexpr std::string_view kGoBuildIdNoteName = "Go\0\0"sv;
constexpr std::string_view kGnuBuildIdNoteName = "GNU\0"sv;
const char *kGoBuildIdSection = ".note.go.buildid";
const char *kGnuBuildIdSection = ".note.gnu.build-id";
const Elf64_Word kGoBuildIdTag = 4;

Elf_Scn *find_note_section(Elf *elf, const char *section_name) {
  size_t stridx;
  if (elf_getshdrstrndx(elf, &stridx) != 0) {
    return nullptr;
  }

  Elf_Scn *section = nullptr;
  GElf_Shdr section_header;
  while ((section = elf_nextscn(elf, section)) != nullptr) {
    if (!gelf_getshdr(section, &section_header) ||
        section_header.sh_type != SHT_NOTE) {
      continue;
    }

    const char *name = elf_strptr(elf, stridx, section_header.sh_name);
    if (name && !strcmp(name, section_name)) {
      return section;
    }
  }

  return nullptr;
}

std::span<const std::byte> process_note(Elf_Data *data, Elf64_Word note_type,
                                        std::string_view note_name) {
  size_t pos = 0;
  GElf_Nhdr note_header;
  size_t name_pos;
  size_t desc_pos;
  while ((pos = gelf_getnote(data, pos, &note_header, &name_pos, &desc_pos)) >
         0) {
    const auto *buf = reinterpret_cast<const std::byte *>(data->d_buf);
    if (note_header.n_type == note_type &&
        note_header.n_namesz == note_name.size() &&
        !memcmp(buf + name_pos, note_name.data(), note_name.size())) {
      return {buf + desc_pos, note_header.n_descsz};
    }
  }
  return {};
}

std::span<const std::byte> get_elf_note(Elf *elf, const char *node_section_name,
                                        Elf64_Word note_type,
                                        std::string_view note_name) {
  Elf_Scn *scn = elf_nextscn(elf, nullptr);

  if (scn) {
    // there is a section hdr, try it first
    Elf_Scn *note_section = find_note_section(elf, node_section_name);
    if (!note_section) {
      return {};
    }

    Elf_Data *data = elf_getdata(note_section, nullptr);
    if (data) {
      auto result = process_note(data, note_type, note_name);
      if (!result.empty()) {
        return result;
      }
    }
  }

  // if we didn't find the note in the sections, try the program headers
  size_t phnum;
  if (elf_getphdrnum(elf, &phnum) != 0) {
    return {};
  }
  for (size_t i = 0; i < phnum; ++i) {
    GElf_Phdr phdr_mem;
    GElf_Phdr *phdr = gelf_getphdr(elf, i, &phdr_mem);
    if (phdr != nullptr && phdr->p_type == PT_NOTE) {
      Elf_Data *data =
          elf_getdata_rawchunk(elf, phdr->p_offset, phdr->p_filesz,
                               (phdr->p_align == 8 ? ELF_T_NHDR8 : ELF_T_NHDR));
      if (data) {
        auto result = process_note(data, note_type, note_name);
        if (!result.empty()) {
          return result;
        }
      }
    }
  }

  return {};
}

std::optional<std::string> get_gnu_build_id(Elf *elf) {
  auto note = get_elf_note(elf, kGnuBuildIdSection, NT_GNU_BUILD_ID,
                           kGnuBuildIdNoteName);
  if (note.empty()) {
    return std::nullopt;
  }

  return format_build_id(BuildIdSpan{
      reinterpret_cast<const unsigned char *>(note.data()), note.size()});
}

std::optional<std::string> get_golang_build_id(Elf *elf) {
  auto note =
      get_elf_note(elf, kGoBuildIdSection, kGoBuildIdTag, kGoBuildIdNoteName);
  if (note.empty()) {
    return std::nullopt;
  }

  return std::string{reinterpret_cast<const char *>(note.data()), note.size()};
}

std::optional<std::string> find_build_id(Elf *elf) {
  auto maybe_gnu_build_id = get_gnu_build_id(elf);
  if (maybe_gnu_build_id) {
    return maybe_gnu_build_id;
  }

  auto maybe_golang_build_id = get_golang_build_id(elf);
  if (maybe_golang_build_id) {
    return maybe_golang_build_id;
  }
  return std::nullopt;
}

DDRes find_elf_segment(Elf *elf, const std::string &filepath,
                       Offset_t file_offset, Segment &segment) {
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
      if (ph->p_type == PT_LOAD && file_offset >= ph->p_offset &&
          file_offset < ph->p_offset + ph->p_filesz) {
        segment =
            Segment{ph->p_vaddr, ph->p_offset, elf_flags_to_prot(ph->p_flags)};
        return {};
      }
    }
    break;
  }
  default:
    LG_WRN("Unsupported elf type (%d) %s", ehdr->e_type, filepath.c_str());
    return ddres_error(DD_WHAT_INVALID_ELF);
  }

  LG_WRN("No LOAD segment found for offset %lx in %s", file_offset,
         filepath.c_str());
  return ddres_error(DD_WHAT_NO_MATCHING_LOAD_SEGMENT);
}

DDRes compute_elf_bias(Elf *elf, const std::string &filepath, const Dso &dso,
                       ProcessAddress_t pc, Offset_t &bias) {
  // Compute file offset from pc
  Offset_t const file_offset = pc - dso.start() + dso.offset();

  Segment segment;
  auto res = find_elf_segment(elf, filepath, file_offset, segment);
  if (!IsDDResOK(res)) {
    return res;
  }

  bias = dso.start() - dso.offset() - (segment.addr - segment.offset);

  return {};
}
} // namespace

DDRes report_module(Dwfl *dwfl, ProcessAddress_t pc, const Dso &dso,
                    const FileInfoValue &fileInfoValue, DDProfMod &ddprof_mod) {
  const std::string &filepath = fileInfoValue.get_path();
  const char *module_name = strrchr(filepath.c_str(), '/') + 1;
  if (fileInfoValue.errored()) { // avoid bouncing on errors
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
           dso._pid, main_name, low_addr, high_addr, pc, filepath.c_str(),
           fileInfoValue.get_id());
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
  Elf *elf = elf_begin(fd_holder.get(), ELF_C_READ_MMAP, nullptr);
  if (elf == nullptr) {
    LG_WRN("Invalid elf %s", filepath.c_str());
    return ddres_error(DD_WHAT_INVALID_ELF);
  }
  defer { elf_end(elf); };

  Offset_t bias = 0;
  auto res = compute_elf_bias(elf, filepath, dso, pc, bias);
  if (!IsDDResOK(res)) {
    fileInfoValue.set_errored();
    LG_WRN("Couldn't retrieve offsets from %s(%s)", module_name,
           fileInfoValue.get_path().c_str());
    return res;
  }

  LG_NFO("Loading module %s for pid %d", fileInfoValue.get_path().c_str(),
         dso._pid);
  ddprof_mod._mod = dwfl_report_elf(dwfl, module_name, filepath.c_str(),
                                    fd_holder.get(), bias, true);

  auto maybe_build_id = find_build_id(elf);

  // Retrieve build id
  if (maybe_build_id) {
    ddprof_mod.set_build_id(std::move(maybe_build_id.value()));
  }

  if (!ddprof_mod._mod) {
    // Ideally we would differentiate pid errors from file errors.
    // For perf reasons we will just flag the file as errored
    fileInfoValue.set_errored();
    LG_WRN("Couldn't addrmodule (%s)[0x%lx], MOD:%s (%s)", dwfl_errmsg(-1), pc,
           module_name, fileInfoValue.get_path().c_str());
    return ddres_warn(DD_WHAT_MODULE);
  }
  // dwfl now has ownership of the file descriptor
  (void)fd_holder.release(); // NOLINT
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

std::optional<std::string> find_build_id(const char *filepath) {
  const UniqueFd fd_holder{::open(filepath, O_RDONLY)};
  if (!fd_holder) {
    return std::nullopt;
  }
  Elf *elf = elf_begin(fd_holder.get(), ELF_C_READ_MMAP, nullptr);
  if (elf == nullptr) {
    return std::nullopt;
  }
  defer { elf_end(elf); };
  return find_build_id(elf);
}

} // namespace ddprof
