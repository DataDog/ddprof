// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <cstdint>
#include <string_view>
#include <sys/types.h>

// libelf forward declaration: callers that pass an Elf* must include
// <libelf.h>.
extern "C" {
struct Elf;
}

namespace ddprof {

// Heuristic native-language family for a process' main executable.
// Only meant to refine the "native" language tag for cases where a single
// language clearly dominates a process. Mixed-language binaries fall back to
// kUnknown (caller is expected to report "native" in that case).
enum class NativeLanguage : uint8_t {
  kUnknown = 0, // fallback -> reported as "native"
  kGo,
  kRust,
  kCpp,
};

// Returns a stable label string for a detected language.
// kUnknown maps to "native" (the existing default tag).
std::string_view to_string(NativeLanguage lang);

// Detect the native language of an already-opened ELF object.
// Intentionally heuristic and cheap:
//   * Go        -> `.go.buildinfo` / `.gopclntab` ELF section
//   * Rust      -> `.note.rustc` section or rustc-mangled symbols in
//                  .dynsym / .symtab (bounded scan)
//   * Cpp       -> any `_Z`-mangled symbol (Itanium ABI) not matching Rust
// Never reads DWARF.
// Returns kUnknown on null input or unrecognised ELF.
NativeLanguage detect_native_language(::Elf *elf);

// Convenience wrapper: open `/proc/<pid>/exe` ourselves. Prefer the Elf*
// overload above when the caller already has a handle (e.g. via libdwfl).
NativeLanguage detect_native_language(pid_t pid, std::string_view path_to_proc);

} // namespace ddprof
