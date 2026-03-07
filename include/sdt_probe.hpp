// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ddprof {

/// Location type for SDT probe arguments
enum class SDTArgLocation : uint8_t {
  kRegister, // Value in a CPU register
  kMemory,   // Value at memory location (base register + offset)
  kConstant, // Constant/immediate value
};

/// Represents a single argument to an SDT probe
/// Arguments are encoded as assembly expressions like "8@%rdi" or "-4@8(%rbp)"
struct SDTArgument {
  int8_t size;          // Size in bytes (negative = signed)
  SDTArgLocation location;
  uint8_t base_reg;     // Base register number (for register or memory)
  uint8_t index_reg;    // Index register (for scaled memory addressing)
  uint8_t scale;        // Scale factor for index register
  int64_t offset;       // Memory offset or constant value
  std::string raw_spec; // Original argument specification
};

/// Represents a discovered SDT probe from .note.stapsdt section
struct SDTProbe {
  std::string provider; // e.g., "ddprof_malloc"
  std::string name;     // e.g., "entry"
  uint64_t address;     // Probe location (virtual address in ELF)
  uint64_t base;        // Base address for prelink adjustment
  uint64_t semaphore;   // Semaphore address (0 if not used)
  std::vector<SDTArgument> arguments;

  /// Get full probe name as "provider:name"
  std::string full_name() const { return provider + ":" + name; }
};

/// Probe types for memory allocation tracking
enum class SDTProbeType : uint8_t {
  kUnknown,
  kMallocEntry,
  kMallocExit,
  kFreeEntry,
  kFreeExit,
};

/// Collection of SDT probes discovered from a binary
struct SDTProbeSet {
  std::string binary_path;
  std::vector<SDTProbe> probes;

  /// Find probes matching a provider and name
  std::vector<const SDTProbe *> find_probes(std::string_view provider,
                                            std::string_view name) const;

  /// Find a single probe by provider and name (returns first match)
  const SDTProbe *find_probe(std::string_view provider,
                             std::string_view name) const;

  /// Check if the probe set contains all required allocation probes
  bool has_allocation_probes() const;

  /// Get the probe type for a probe
  static SDTProbeType get_probe_type(const SDTProbe &probe);
};

/// Parse SDT probes from an ELF binary file
/// Returns nullopt if no probes found or file cannot be read
std::optional<SDTProbeSet> parse_sdt_probes(const char *filepath);

/// Parse a single SDT argument specification
/// Format: [+-]?size@location where location can be:
///   - %reg (register)
///   - constant (immediate value)
///   - offset(%reg) (memory reference)
///   - offset(%base,%index,scale) (scaled indexed addressing)
std::optional<SDTArgument> parse_sdt_argument(std::string_view arg_spec);

/// Convert x86-64 register name to perf register number
/// Returns -1 if register name is not recognized
int x86_reg_name_to_perf_reg(std::string_view reg_name);

/// Provider name for malloc probes
inline constexpr std::string_view kMallocProvider = "ddprof_malloc";
/// Provider name for free probes
inline constexpr std::string_view kFreeProvider = "ddprof_free";
/// Entry probe name
inline constexpr std::string_view kEntryProbe = "entry";
/// Exit probe name
inline constexpr std::string_view kExitProbe = "exit";

} // namespace ddprof
