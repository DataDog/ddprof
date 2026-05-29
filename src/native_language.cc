// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "native_language.hpp"

#include "logger.hpp"
#include "unique_fd.hpp"

#include <absl/strings/str_cat.h>
#include <cstring>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <string>
#include <string_view>

namespace ddprof {

namespace {

constexpr size_t k_max_symbols_scanned = 4096;

// Returns true if `name` looks like a rustc-mangled symbol.
//   * v0 mangling: starts with "_R"
//   * legacy mangling: Itanium "_ZN...17h<16 hex chars>E" tail
bool looks_like_rust_symbol(std::string_view name) {
  if (name.size() > 2 && name[0] == '_' && name[1] == 'R') {
    return true;
  }
  // Legacy mangling: ..."17h" + 16 hex + "E" at the very end.
  // We don't need to validate the full Itanium prefix, the tail is unique
  // enough for a heuristic.
  constexpr size_t k_tail = 20; // "17h" + 16 hex + "E"
  if (name.size() < k_tail || name.back() != 'E') {
    return false;
  }
  const size_t pos = name.size() - k_tail;
  if (name.compare(pos, 3, "17h") != 0) {
    return false;
  }
  for (size_t i = pos + 3; i < name.size() - 1; ++i) {
    const char c = name[i];
    const bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!is_hex) {
      return false;
    }
  }
  return true;
}

bool looks_like_cpp_symbol(std::string_view name) {
  // Itanium C++ mangling: "_Z..." (and we already ruled out Rust legacy above).
  return name.size() > 2 && name[0] == '_' && name[1] == 'Z';
}

// Scan a symbol table section. Returns true and sets `out` if a definitive
// signal is found. Bails after k_max_symbols_scanned entries.
bool scan_symtab(Elf *elf, Elf_Scn *scn, GElf_Shdr const &shdr,
                 NativeLanguage &out) {
  Elf_Data *data = elf_getdata(scn, nullptr);
  if (data == nullptr || shdr.sh_entsize == 0) {
    return false;
  }
  const size_t nsyms = shdr.sh_size / shdr.sh_entsize;
  bool saw_cpp = false;
  size_t scanned = 0;
  for (size_t i = 0; i < nsyms && scanned < k_max_symbols_scanned; ++i) {
    GElf_Sym sym;
    if (gelf_getsym(data, static_cast<int>(i), &sym) == nullptr) {
      continue;
    }
    const char *raw = elf_strptr(elf, shdr.sh_link, sym.st_name);
    if (raw == nullptr || raw[0] == '\0') {
      continue;
    }
    ++scanned;
    std::string_view const name(raw);
    if (looks_like_rust_symbol(name)) {
      out = NativeLanguage::kRust;
      return true; // Rust wins immediately
    }
    if (!saw_cpp && looks_like_cpp_symbol(name)) {
      saw_cpp = true;
    }
  }
  if (saw_cpp) {
    out = NativeLanguage::kCpp;
    return true;
  }
  return false;
}

NativeLanguage detect_from_elf_impl(Elf *elf) {
  // First pass: section-name probes (cheapest).
  size_t shstrndx = 0;
  if (elf_getshdrstrndx(elf, &shstrndx) != 0) {
    return NativeLanguage::kUnknown;
  }

  Elf_Scn *symtab_scn = nullptr;
  GElf_Shdr symtab_shdr{};
  Elf_Scn *dynsym_scn = nullptr;
  GElf_Shdr dynsym_shdr{};

  Elf_Scn *scn = nullptr;
  while ((scn = elf_nextscn(elf, scn)) != nullptr) {
    GElf_Shdr shdr;
    if (gelf_getshdr(scn, &shdr) == nullptr) {
      continue;
    }
    const char *name = elf_strptr(elf, shstrndx, shdr.sh_name);
    if (name == nullptr) {
      continue;
    }
    std::string_view const sname(name);
    if (sname == ".go.buildinfo" || sname == ".gopclntab") {
      return NativeLanguage::kGo;
    }
    if (sname == ".note.rustc") {
      return NativeLanguage::kRust;
    }
    if (shdr.sh_type == SHT_SYMTAB) {
      symtab_scn = scn;
      symtab_shdr = shdr;
    } else if (shdr.sh_type == SHT_DYNSYM) {
      dynsym_scn = scn;
      dynsym_shdr = shdr;
    }
  }

  // Second pass: symbol-table heuristics. Prefer .symtab (richer); fall back
  // to .dynsym for stripped binaries.
  NativeLanguage out = NativeLanguage::kUnknown;
  if (symtab_scn != nullptr && scan_symtab(elf, symtab_scn, symtab_shdr, out)) {
    return out;
  }
  if (dynsym_scn != nullptr && scan_symtab(elf, dynsym_scn, dynsym_shdr, out)) {
    return out;
  }
  return NativeLanguage::kUnknown;
}

} // namespace

NativeLanguage detect_native_language(::Elf *elf) {
  if (elf == nullptr) {
    return NativeLanguage::kUnknown;
  }
  // Safety: libelf needs to have been initialised. ddprof calls
  // elf_version(EV_CURRENT) in unwind_init(); make it idempotent here too.
  elf_version(EV_CURRENT);
  if (elf_kind(elf) != ELF_K_ELF) {
    return NativeLanguage::kUnknown;
  }
  NativeLanguage const result = detect_from_elf_impl(elf);
  LG_DBG("[NATIVE-LANG] -> %s", std::string(to_string(result)).c_str());
  return result;
}

std::string_view to_string(NativeLanguage lang) {
  switch (lang) {
  case NativeLanguage::kGo:
    return "go";
  case NativeLanguage::kRust:
    return "rust";
  case NativeLanguage::kCpp:
    return "cpp";
  case NativeLanguage::kUnknown:
  default:
    return "native";
  }
}

NativeLanguage detect_native_language(pid_t pid,
                                      std::string_view path_to_proc) {
  // libelf must be initialised; ddprof already calls elf_version() in
  // unwind_init(), but make it idempotent-safe here in case this is invoked
  // from a context where it has not been.
  elf_version(EV_CURRENT);

  const std::string exe_path =
      absl::StrCat(path_to_proc, "/proc/", pid, "/exe");
  const UniqueFd fd{::open(exe_path.c_str(), O_RDONLY | O_CLOEXEC)};
  if (!fd) {
    return NativeLanguage::kUnknown;
  }
  Elf *elf = elf_begin(fd.get(), ELF_C_READ_MMAP, nullptr);
  if (elf == nullptr) {
    return NativeLanguage::kUnknown;
  }
  NativeLanguage result = NativeLanguage::kUnknown;
  if (elf_kind(elf) == ELF_K_ELF) {
    result = detect_from_elf_impl(elf);
  }
  elf_end(elf);
  LG_DBG("[NATIVE-LANG] (from /proc) pid=%d -> %s", pid,
         std::string(to_string(result)).c_str());
  return result;
}

} // namespace ddprof
