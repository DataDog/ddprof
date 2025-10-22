// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
//
// Implemented in the scope of this issue.
// Disucssion here https://github.com/DataDog/ddprof/issues/212
//
// Another implementations of this is available in Go
// https://github.com/parca-dev/parca-agent/blob/4538c7f6c0b5e686cbdde2594c422edf98432c23/pkg/jit/jitdump.go
// Thanks to @maxbrunet for a reference implementation a and well commented code
//
// Some other notes around jvmti
// https://github.com/sfriberg/perf-jitdump-agent
//
// Some notes around the format (thanks to Stephane Eranian)
// https://github.dev/torvalds/linux/blob/ab072681eabe1ce0a9a32d4baa1a27a2d046bc4a/tools/perf/Documentation/jitdump-specification.txt#L8
//
#pragma once

#include "ddres.hpp"

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace ddprof {

struct JITHeader {
  uint32_t magic;      /* characters "jItD" */
  uint32_t version;    /* header version */
  uint32_t total_size; /* total size of header */
  uint32_t elf_mach;   /* elf mach target */
  uint32_t pad1;       /* reserved */
  uint32_t pid;        /* JIT process id */
  uint64_t timestamp;  /* timestamp */
  uint64_t flags;      /* flags */
};

static constexpr int k_jit_header_version = 1;

// Looking at LLVM code, only DEBUG and LOAD are emitted
// https://github.com/llvm/llvm-project/blob/main/llvm/lib/ExecutionEngine/PerfJITEvents/PerfJITEventListener.cpp
enum JITRecordType : uint8_t {
  JIT_CODE_LOAD = 0,           // record describing a jitted function
  JIT_CODE_MOVE = 1,           // already jitted function which is moved
  JIT_CODE_DEBUG_INFO = 2,     // debug information for a jitted function
  JIT_CODE_CLOSE = 3,          // end of the jit runtime (optional)
  JIT_CODE_UNWINDING_INFO = 4, // function unwinding information
  JIT_CODE_MAX = 5             // maximum record type
};

/* At the start of every record */
struct JITRecordPrefix {
  uint32_t id; // JITRecordType (leaving it as uint as size is important)
  uint32_t total_size;
  uint64_t timestamp; // Not used for now (nice info to order events)
};

struct JITRecordCodeLoad {
  // minimal size we will read
  static constexpr uint32_t k_size_integers =
      (sizeof(uint32_t) * 2) + (sizeof(uint64_t) * 4);
  JITRecordPrefix prefix;
  uint32_t pid;
  uint32_t tid;
  uint64_t vma;
  uint64_t code_addr;
  uint64_t code_size;
  uint64_t code_index;
  std::string func_name;
  std::byte *raw_code; // not sure how this can be useful for now
};

#ifdef EXTENDED_JITDUMP_STRUCTS
// Following structures are part of the spec, though not used for now
// LLVM is not emitting these structures

struct JITRecordCodeClose {
  struct JITRecordPrefix p;
};

// Unused (as not emitted by LLVM as of now)
struct JITRecordCodeMove {
  struct JITRecordPrefix prefix;
  uint32_t pid;
  uint32_t tid;
  uint64_t vma;
  uint64_t old_code_addr;
  uint64_t new_code_addr;
  uint64_t code_size;
  uint64_t code_index;
};

// Unused (as not emitted by LLVM as of now)
struct JITRecordUnwindingInfo {
  struct JITRecordPrefix prefix;
  uint64_t unwinding_size;
  uint64_t eh_frame_hdr_size;
  uint64_t mapped_size;
  std::vector<char> unwinding_data;
};
#endif

struct DebugEntry {
  uint64_t addr;
  int32_t lineno; /* source line number starting at 1 */
  int32_t discrim;
  std::string name;
};

struct JITRecordDebugInfo {
  // minimal size we will read
  static constexpr uint32_t k_size_integers = sizeof(uint64_t) * 2;
  JITRecordPrefix prefix;
  uint64_t code_addr;
  uint64_t nr_entry;
  std::vector<DebugEntry> entries;
};

struct JITDump {
  JITHeader header;
  std::vector<JITRecordCodeLoad> code_load;
  std::vector<JITRecordDebugInfo> debug_info;
};

DDRes jitdump_read(std::string_view file, JITDump &jit_dump);

} // namespace ddprof
