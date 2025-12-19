// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "sdt_probe.hpp"

#include "defer.hpp"
#include "logger.hpp"
#include "unique_fd.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <unistd.h>

using namespace std::literals;

namespace ddprof {

namespace {

// Note type for SystemTap SDT probes
constexpr Elf64_Word kNtStapsdt = 3;

// Note name for SDT probes (null-terminated, padded to 4-byte alignment)
constexpr std::string_view kStapsdtNoteName = "stapsdt\0"sv;

// Section name for SDT notes
constexpr const char *kStapsdtSectionName = ".note.stapsdt";

/// x86-64 register name to PERF_REG_X86_* mapping
/// Based on linux/arch/x86/include/uapi/asm/perf_regs.h
struct RegMapping {
  const char *name;
  int perf_reg;
};

// PERF_REG_X86_* values from perf_regs.h
enum {
  PERF_REG_X86_AX = 0,
  PERF_REG_X86_BX = 1,
  PERF_REG_X86_CX = 2,
  PERF_REG_X86_DX = 3,
  PERF_REG_X86_SI = 4,
  PERF_REG_X86_DI = 5,
  PERF_REG_X86_BP = 6,
  PERF_REG_X86_SP = 7,
  PERF_REG_X86_IP = 8,
  PERF_REG_X86_FLAGS = 9,
  PERF_REG_X86_CS = 10,
  PERF_REG_X86_SS = 11,
  PERF_REG_X86_DS = 12,
  PERF_REG_X86_ES = 13,
  PERF_REG_X86_FS = 14,
  PERF_REG_X86_GS = 15,
  PERF_REG_X86_R8 = 16,
  PERF_REG_X86_R9 = 17,
  PERF_REG_X86_R10 = 18,
  PERF_REG_X86_R11 = 19,
  PERF_REG_X86_R12 = 20,
  PERF_REG_X86_R13 = 21,
  PERF_REG_X86_R14 = 22,
  PERF_REG_X86_R15 = 23,
};

// Register mappings for x86-64
// Includes both 64-bit and 32-bit register names
static const RegMapping kX86RegMappings[] = {
    // 64-bit registers
    {"rax", PERF_REG_X86_AX},
    {"rbx", PERF_REG_X86_BX},
    {"rcx", PERF_REG_X86_CX},
    {"rdx", PERF_REG_X86_DX},
    {"rsi", PERF_REG_X86_SI},
    {"rdi", PERF_REG_X86_DI},
    {"rbp", PERF_REG_X86_BP},
    {"rsp", PERF_REG_X86_SP},
    {"rip", PERF_REG_X86_IP},
    {"r8", PERF_REG_X86_R8},
    {"r9", PERF_REG_X86_R9},
    {"r10", PERF_REG_X86_R10},
    {"r11", PERF_REG_X86_R11},
    {"r12", PERF_REG_X86_R12},
    {"r13", PERF_REG_X86_R13},
    {"r14", PERF_REG_X86_R14},
    {"r15", PERF_REG_X86_R15},
    // 32-bit registers (lower 32 bits of 64-bit regs)
    {"eax", PERF_REG_X86_AX},
    {"ebx", PERF_REG_X86_BX},
    {"ecx", PERF_REG_X86_CX},
    {"edx", PERF_REG_X86_DX},
    {"esi", PERF_REG_X86_SI},
    {"edi", PERF_REG_X86_DI},
    {"ebp", PERF_REG_X86_BP},
    {"esp", PERF_REG_X86_SP},
    {"r8d", PERF_REG_X86_R8},
    {"r9d", PERF_REG_X86_R9},
    {"r10d", PERF_REG_X86_R10},
    {"r11d", PERF_REG_X86_R11},
    {"r12d", PERF_REG_X86_R12},
    {"r13d", PERF_REG_X86_R13},
    {"r14d", PERF_REG_X86_R14},
    {"r15d", PERF_REG_X86_R15},
    // 16-bit registers
    {"ax", PERF_REG_X86_AX},
    {"bx", PERF_REG_X86_BX},
    {"cx", PERF_REG_X86_CX},
    {"dx", PERF_REG_X86_DX},
    {"si", PERF_REG_X86_SI},
    {"di", PERF_REG_X86_DI},
    {"bp", PERF_REG_X86_BP},
    {"sp", PERF_REG_X86_SP},
    // 8-bit registers (low bytes)
    {"al", PERF_REG_X86_AX},
    {"bl", PERF_REG_X86_BX},
    {"cl", PERF_REG_X86_CX},
    {"dl", PERF_REG_X86_DX},
    {"sil", PERF_REG_X86_SI},
    {"dil", PERF_REG_X86_DI},
    {"bpl", PERF_REG_X86_BP},
    {"spl", PERF_REG_X86_SP},
    {"r8b", PERF_REG_X86_R8},
    {"r9b", PERF_REG_X86_R9},
    {"r10b", PERF_REG_X86_R10},
    {"r11b", PERF_REG_X86_R11},
    {"r12b", PERF_REG_X86_R12},
    {"r13b", PERF_REG_X86_R13},
    {"r14b", PERF_REG_X86_R14},
    {"r15b", PERF_REG_X86_R15},
};

/// Find note section by name
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

/// Parse SDT note descriptor to extract probe information
/// Returns true on success
bool parse_sdt_note_desc(const std::byte *desc, size_t desc_size,
                         bool is_64bit, SDTProbe &probe) {
  const size_t addr_size = is_64bit ? 8 : 4;
  const size_t min_size = 3 * addr_size; // pc, base, semaphore

  if (desc_size < min_size) {
    LG_DBG("SDT note descriptor too small: %zu < %zu", desc_size, min_size);
    return false;
  }

  // Read fixed fields (3 addresses)
  size_t pos = 0;

  if (is_64bit) {
    uint64_t val;
    memcpy(&val, desc + pos, sizeof(val));
    probe.address = val;
    pos += sizeof(val);

    memcpy(&val, desc + pos, sizeof(val));
    probe.base = val;
    pos += sizeof(val);

    memcpy(&val, desc + pos, sizeof(val));
    probe.semaphore = val;
    pos += sizeof(val);
  } else {
    uint32_t val;
    memcpy(&val, desc + pos, sizeof(val));
    probe.address = val;
    pos += sizeof(val);

    memcpy(&val, desc + pos, sizeof(val));
    probe.base = val;
    pos += sizeof(val);

    memcpy(&val, desc + pos, sizeof(val));
    probe.semaphore = val;
    pos += sizeof(val);
  }

  // Read null-terminated strings: provider, name, arguments
  const char *str_ptr = reinterpret_cast<const char *>(desc + pos);
  const char *end_ptr = reinterpret_cast<const char *>(desc + desc_size);

  // Provider
  size_t len = strnlen(str_ptr, end_ptr - str_ptr);
  if (str_ptr + len >= end_ptr) {
    LG_DBG("SDT note: provider string not terminated");
    return false;
  }
  probe.provider = std::string(str_ptr, len);
  str_ptr += len + 1;

  // Name
  len = strnlen(str_ptr, end_ptr - str_ptr);
  if (str_ptr + len >= end_ptr) {
    LG_DBG("SDT note: probe name string not terminated");
    return false;
  }
  probe.name = std::string(str_ptr, len);
  str_ptr += len + 1;

  // Arguments (may be empty)
  len = strnlen(str_ptr, end_ptr - str_ptr);
  std::string args_str(str_ptr, len);

  // Parse space-separated arguments
  if (!args_str.empty()) {
    size_t arg_start = 0;
    while (arg_start < args_str.size()) {
      // Skip leading spaces
      while (arg_start < args_str.size() && args_str[arg_start] == ' ') {
        ++arg_start;
      }
      if (arg_start >= args_str.size()) {
        break;
      }

      // Find end of argument
      size_t arg_end = args_str.find(' ', arg_start);
      if (arg_end == std::string::npos) {
        arg_end = args_str.size();
      }

      std::string_view arg_spec(args_str.data() + arg_start,
                                arg_end - arg_start);
      if (auto arg = parse_sdt_argument(arg_spec)) {
        probe.arguments.push_back(std::move(*arg));
      } else {
        LG_DBG("Failed to parse SDT argument: %.*s",
               static_cast<int>(arg_spec.size()), arg_spec.data());
      }

      arg_start = arg_end + 1;
    }
  }

  return true;
}

/// Parse SDT probes from an ELF handle
std::optional<SDTProbeSet> parse_sdt_probes_from_elf(Elf *elf,
                                                     const char *filepath) {
  // Determine if 64-bit ELF
  GElf_Ehdr ehdr;
  if (!gelf_getehdr(elf, &ehdr)) {
    LG_DBG("Failed to get ELF header for %s", filepath);
    return std::nullopt;
  }
  bool is_64bit = (ehdr.e_ident[EI_CLASS] == ELFCLASS64);

  // Find .note.stapsdt section
  Elf_Scn *note_scn = find_note_section(elf, kStapsdtSectionName);
  if (!note_scn) {
    LG_DBG("No %s section found in %s", kStapsdtSectionName, filepath);
    return std::nullopt;
  }

  Elf_Data *data = elf_getdata(note_scn, nullptr);
  if (!data || data->d_size == 0) {
    LG_DBG("Empty %s section in %s", kStapsdtSectionName, filepath);
    return std::nullopt;
  }

  SDTProbeSet result;
  result.binary_path = filepath;

  // Iterate over notes in the section
  size_t pos = 0;
  GElf_Nhdr note_header;
  size_t name_pos;
  size_t desc_pos;

  while ((pos = gelf_getnote(data, pos, &note_header, &name_pos, &desc_pos)) >
         0) {
    // Check note type and name
    if (note_header.n_type != kNtStapsdt) {
      continue;
    }

    // Verify note name is "stapsdt"
    const char *note_name =
        reinterpret_cast<const char *>(data->d_buf) + name_pos;
    if (note_header.n_namesz != kStapsdtNoteName.size() ||
        memcmp(note_name, kStapsdtNoteName.data(), kStapsdtNoteName.size()) !=
            0) {
      continue;
    }

    // Parse the descriptor
    const auto *desc =
        reinterpret_cast<const std::byte *>(data->d_buf) + desc_pos;

    SDTProbe probe;
    if (parse_sdt_note_desc(desc, note_header.n_descsz, is_64bit, probe)) {
      LG_DBG("Found SDT probe: %s:%s at 0x%lx", probe.provider.c_str(),
             probe.name.c_str(), probe.address);
      result.probes.push_back(std::move(probe));
    }
  }

  if (result.probes.empty()) {
    return std::nullopt;
  }

  return result;
}

} // anonymous namespace

int x86_reg_name_to_perf_reg(std::string_view reg_name) {
  for (const auto &mapping : kX86RegMappings) {
    if (reg_name == mapping.name) {
      return mapping.perf_reg;
    }
  }
  return -1;
}

std::optional<SDTArgument> parse_sdt_argument(std::string_view arg_spec) {
  // Format: [+-]?size@location
  // Examples: "8@%rdi", "-4@%esi", "8@-8(%rbp)", "-4@$42"

  if (arg_spec.empty()) {
    return std::nullopt;
  }

  SDTArgument arg;
  arg.raw_spec = std::string(arg_spec);
  arg.base_reg = 0;
  arg.index_reg = 0;
  arg.scale = 0;
  arg.offset = 0;

  // Find @ separator
  auto at_pos = arg_spec.find('@');
  if (at_pos == std::string_view::npos || at_pos == 0) {
    LG_DBG("Invalid SDT argument format (no @): %.*s",
           static_cast<int>(arg_spec.size()), arg_spec.data());
    return std::nullopt;
  }

  // Parse size (before @)
  auto size_str = arg_spec.substr(0, at_pos);
  int size_val = 0;
  auto [ptr, ec] = std::from_chars(size_str.data(),
                                   size_str.data() + size_str.size(), size_val);
  if (ec != std::errc{} || ptr != size_str.data() + size_str.size()) {
    LG_DBG("Invalid SDT argument size: %.*s", static_cast<int>(size_str.size()),
           size_str.data());
    return std::nullopt;
  }
  arg.size = static_cast<int8_t>(size_val);

  // Parse location (after @)
  auto loc_str = arg_spec.substr(at_pos + 1);
  if (loc_str.empty()) {
    LG_DBG("Empty SDT argument location");
    return std::nullopt;
  }

  if (loc_str[0] == '%') {
    // Register: %rdi, %rax, etc.
    arg.location = SDTArgLocation::kRegister;
    auto reg_name = loc_str.substr(1);
    int reg_num = x86_reg_name_to_perf_reg(reg_name);
    if (reg_num < 0) {
      LG_DBG("Unknown register in SDT argument: %.*s",
             static_cast<int>(reg_name.size()), reg_name.data());
      return std::nullopt;
    }
    arg.base_reg = static_cast<uint8_t>(reg_num);
  } else if (loc_str[0] == '$') {
    // Constant: $42
    arg.location = SDTArgLocation::kConstant;
    auto const_str = loc_str.substr(1);
    int64_t const_val = 0;
    auto [p, e] = std::from_chars(
        const_str.data(), const_str.data() + const_str.size(), const_val);
    if (e != std::errc{}) {
      LG_DBG("Invalid constant in SDT argument: %.*s",
             static_cast<int>(const_str.size()), const_str.data());
      return std::nullopt;
    }
    arg.offset = const_val;
  } else {
    // Memory reference: offset(%reg) or offset(%base,%index,scale)
    arg.location = SDTArgLocation::kMemory;

    // Find the opening parenthesis
    auto paren_pos = loc_str.find('(');
    if (paren_pos == std::string_view::npos) {
      // No parenthesis - could be a bare constant or symbol
      // Try to parse as constant
      int64_t val = 0;
      auto [p, e] =
          std::from_chars(loc_str.data(), loc_str.data() + loc_str.size(), val);
      if (e == std::errc{} && p == loc_str.data() + loc_str.size()) {
        arg.location = SDTArgLocation::kConstant;
        arg.offset = val;
        return arg;
      }
      LG_DBG("Unsupported SDT argument location format: %.*s",
             static_cast<int>(loc_str.size()), loc_str.data());
      return std::nullopt;
    }

    // Parse offset before parenthesis
    if (paren_pos > 0) {
      auto offset_str = loc_str.substr(0, paren_pos);
      int64_t offset_val = 0;
      auto [p, e] = std::from_chars(
          offset_str.data(), offset_str.data() + offset_str.size(), offset_val);
      if (e != std::errc{}) {
        LG_DBG("Invalid offset in SDT memory argument: %.*s",
               static_cast<int>(offset_str.size()), offset_str.data());
        return std::nullopt;
      }
      arg.offset = offset_val;
    }

    // Find closing parenthesis
    auto close_paren = loc_str.find(')', paren_pos);
    if (close_paren == std::string_view::npos) {
      LG_DBG("Missing closing parenthesis in SDT argument: %.*s",
             static_cast<int>(loc_str.size()), loc_str.data());
      return std::nullopt;
    }

    // Parse register(s) inside parentheses
    auto regs_str = loc_str.substr(paren_pos + 1, close_paren - paren_pos - 1);

    // Check for comma (indicates base,index,scale format)
    auto comma_pos = regs_str.find(',');
    if (comma_pos == std::string_view::npos) {
      // Simple format: (%reg)
      if (regs_str.empty() || regs_str[0] != '%') {
        LG_DBG("Invalid register format in SDT argument: %.*s",
               static_cast<int>(regs_str.size()), regs_str.data());
        return std::nullopt;
      }
      auto reg_name = regs_str.substr(1);
      int reg_num = x86_reg_name_to_perf_reg(reg_name);
      if (reg_num < 0) {
        LG_DBG("Unknown register in SDT memory argument: %.*s",
               static_cast<int>(reg_name.size()), reg_name.data());
        return std::nullopt;
      }
      arg.base_reg = static_cast<uint8_t>(reg_num);
    } else {
      // Complex format: (%base,%index,scale) or (%base,%index)
      // For now, we only support simple offset(%base) format
      // Log a warning but try to parse just the base register
      LG_DBG("Scaled indexed addressing not fully supported: %.*s",
             static_cast<int>(loc_str.size()), loc_str.data());

      auto base_str = regs_str.substr(0, comma_pos);
      if (base_str.empty() || base_str[0] != '%') {
        return std::nullopt;
      }
      auto reg_name = base_str.substr(1);
      int reg_num = x86_reg_name_to_perf_reg(reg_name);
      if (reg_num < 0) {
        return std::nullopt;
      }
      arg.base_reg = static_cast<uint8_t>(reg_num);
    }
  }

  return arg;
}

std::optional<SDTProbeSet> parse_sdt_probes(const char *filepath) {
  if (!filepath || filepath[0] == '\0') {
    LG_DBG("Empty filepath for SDT probe parsing");
    return std::nullopt;
  }

  UniqueFd fd{::open(filepath, O_RDONLY)};
  if (!fd) {
    LG_DBG("Failed to open %s for SDT probe parsing: %s", filepath,
           strerror(errno));
    return std::nullopt;
  }

  // Initialize libelf if needed
  if (elf_version(EV_CURRENT) == EV_NONE) {
    LG_ERR("ELF library initialization failed: %s", elf_errmsg(-1));
    return std::nullopt;
  }

  Elf *elf = elf_begin(fd.get(), ELF_C_READ_MMAP, nullptr);
  if (!elf) {
    LG_DBG("Failed to open ELF file %s: %s", filepath, elf_errmsg(-1));
    return std::nullopt;
  }
  defer { elf_end(elf); };

  // Check that it's actually an ELF file
  if (elf_kind(elf) != ELF_K_ELF) {
    LG_DBG("%s is not an ELF file", filepath);
    return std::nullopt;
  }

  return parse_sdt_probes_from_elf(elf, filepath);
}

std::vector<const SDTProbe *>
SDTProbeSet::find_probes(std::string_view provider,
                         std::string_view name) const {
  std::vector<const SDTProbe *> result;
  for (const auto &probe : probes) {
    if (probe.provider == provider && probe.name == name) {
      result.push_back(&probe);
    }
  }
  return result;
}

const SDTProbe *SDTProbeSet::find_probe(std::string_view provider,
                                        std::string_view name) const {
  for (const auto &probe : probes) {
    if (probe.provider == provider && probe.name == name) {
      return &probe;
    }
  }
  return nullptr;
}

bool SDTProbeSet::has_allocation_probes() const {
  // Check for required probes: malloc entry/exit and free entry
  // free exit is optional
  bool has_malloc_entry = find_probe(kMallocProvider, kEntryProbe) != nullptr;
  bool has_malloc_exit = find_probe(kMallocProvider, kExitProbe) != nullptr;
  bool has_free_entry = find_probe(kFreeProvider, kEntryProbe) != nullptr;

  if (!has_malloc_entry) {
    LG_DBG("Missing SDT probe: %.*s:%.*s",
           static_cast<int>(kMallocProvider.size()), kMallocProvider.data(),
           static_cast<int>(kEntryProbe.size()), kEntryProbe.data());
  }
  if (!has_malloc_exit) {
    LG_DBG("Missing SDT probe: %.*s:%.*s",
           static_cast<int>(kMallocProvider.size()), kMallocProvider.data(),
           static_cast<int>(kExitProbe.size()), kExitProbe.data());
  }
  if (!has_free_entry) {
    LG_DBG("Missing SDT probe: %.*s:%.*s",
           static_cast<int>(kFreeProvider.size()), kFreeProvider.data(),
           static_cast<int>(kEntryProbe.size()), kEntryProbe.data());
  }

  return has_malloc_entry && has_malloc_exit && has_free_entry;
}

SDTProbeType SDTProbeSet::get_probe_type(const SDTProbe &probe) {
  if (probe.provider == kMallocProvider) {
    if (probe.name == kEntryProbe) {
      return SDTProbeType::kMallocEntry;
    }
    if (probe.name == kExitProbe) {
      return SDTProbeType::kMallocExit;
    }
  } else if (probe.provider == kFreeProvider) {
    if (probe.name == kEntryProbe) {
      return SDTProbeType::kFreeEntry;
    }
    if (probe.name == kExitProbe) {
      return SDTProbeType::kFreeExit;
    }
  }
  return SDTProbeType::kUnknown;
}

} // namespace ddprof
