// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "uprobe_attacher.hpp"

#include "ddres.hpp"
#include "defer.hpp"
#include "logger.hpp"
#include "perf.hpp"
#include "perf_archmap.hpp"
#include "unique_fd.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <gelf.h>
#include <libelf.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace ddprof {

namespace {

// Path to uprobe PMU type in sysfs
constexpr const char *kUprobePmuTypePath =
    "/sys/bus/event_source/devices/uprobe/type";

// Bit to set in config for return probes
constexpr uint64_t kRetprobeBit = 1ULL << 0;

// Path to uprobe retprobe bit offset
constexpr const char *kUprobeRetprobeBitPath =
    "/sys/bus/event_source/devices/uprobe/format/retprobe";

/// Read an integer value from a sysfs file
std::optional<uint32_t> read_sysfs_uint(const char *path) {
  std::ifstream f(path);
  if (!f) {
    return std::nullopt;
  }
  uint32_t value;
  if (!(f >> value)) {
    return std::nullopt;
  }
  return value;
}

/// Parse retprobe bit position from format file
/// Format is "config:N" where N is the bit position
std::optional<int> read_retprobe_bit() {
  std::ifstream f(kUprobeRetprobeBitPath);
  if (!f) {
    // Default to bit 0 if file doesn't exist
    return 0;
  }
  std::string line;
  if (!std::getline(f, line)) {
    return 0;
  }
  // Parse "config:N"
  auto colon_pos = line.find(':');
  if (colon_pos == std::string::npos) {
    return 0;
  }
  try {
    return std::stoi(line.substr(colon_pos + 1));
  } catch (...) {
    return 0;
  }
}

} // anonymous namespace

std::optional<uint32_t> read_uprobe_pmu_type() {
  return read_sysfs_uint(kUprobePmuTypePath);
}

DDRes vaddr_to_file_offset(const char *binary_path, uint64_t vaddr,
                           uint64_t *offset) {
  UniqueFd fd{::open(binary_path, O_RDONLY)};
  if (!fd) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN, "Failed to open %s: %s",
                           binary_path, strerror(errno));
  }

  if (elf_version(EV_CURRENT) == EV_NONE) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN, "ELF library init failed: %s",
                           elf_errmsg(-1));
  }

  Elf *elf = elf_begin(fd.get(), ELF_C_READ_MMAP, nullptr);
  if (!elf) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN, "Failed to open ELF %s: %s",
                           binary_path, elf_errmsg(-1));
  }
  defer { elf_end(elf); };

  GElf_Ehdr ehdr;
  if (!gelf_getehdr(elf, &ehdr)) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN, "Failed to get ELF header: %s",
                           elf_errmsg(-1));
  }

  // Find the segment containing this virtual address
  size_t phnum;
  if (elf_getphdrnum(elf, &phnum) != 0) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Failed to get program header count: %s",
                           elf_errmsg(-1));
  }

  for (size_t i = 0; i < phnum; ++i) {
    GElf_Phdr phdr;
    if (!gelf_getphdr(elf, i, &phdr)) {
      continue;
    }

    if (phdr.p_type != PT_LOAD) {
      continue;
    }

    // Check if vaddr falls within this segment's virtual address range
    if (vaddr >= phdr.p_vaddr && vaddr < phdr.p_vaddr + phdr.p_memsz) {
      // Convert to file offset
      *offset = phdr.p_offset + (vaddr - phdr.p_vaddr);
      LG_DBG("Converted vaddr 0x%lx to file offset 0x%lx in %s", vaddr, *offset,
             binary_path);
      return {};
    }
  }

  DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                         "No segment found for vaddr 0x%lx in %s", vaddr,
                         binary_path);
}

UprobeAttacher::UprobeAttacher() = default;

UprobeAttacher::~UprobeAttacher() { detach_all(); }

UprobeAttacher::UprobeAttacher(UprobeAttacher &&other) noexcept
    : _uprobe_type(other._uprobe_type),
      _attachments(std::move(other._attachments)) {
  other._attachments.clear();
}

UprobeAttacher &UprobeAttacher::operator=(UprobeAttacher &&other) noexcept {
  if (this != &other) {
    detach_all();
    _uprobe_type = other._uprobe_type;
    _attachments = std::move(other._attachments);
    other._attachments.clear();
  }
  return *this;
}

std::optional<uint32_t> UprobeAttacher::get_uprobe_type() {
  if (!_uprobe_type) {
    _uprobe_type = read_uprobe_pmu_type();
    if (_uprobe_type) {
      LG_DBG("Uprobe PMU type: %u", *_uprobe_type);
    } else {
      LG_DBG("Uprobe PMU type not available");
    }
  }
  return _uprobe_type;
}

DDRes UprobeAttacher::attach(const UprobeConfig &config,
                             UprobeAttachment *out) {
  auto uprobe_type = get_uprobe_type();
  if (!uprobe_type) {
    DDRES_RETURN_ERROR_LOG(
        DD_WHAT_PERFOPEN,
        "Uprobe PMU type not available - kernel may not support uprobes");
  }

  // Get retprobe bit position
  static std::optional<int> retprobe_bit = read_retprobe_bit();
  if (!retprobe_bit) {
    retprobe_bit = 0;
  }

  struct perf_event_attr attr = {};
  attr.size = sizeof(attr);
  attr.type = *uprobe_type;

  // For uprobe PMU:
  // - config encodes whether this is a retprobe
  // - config1 is the path to the binary (pointer)
  // - config2 is the offset within the binary

  if (config.is_return_probe) {
    attr.config = 1ULL << *retprobe_bit;
  }

  // The binary path needs to remain valid for the lifetime of the perf event
  // Store path as config1 (kernel will copy it)
  attr.config1 = reinterpret_cast<uint64_t>(config.binary_path.c_str());
  attr.config2 = config.offset;

  // Sample configuration
  attr.sample_type = PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_IP |
                     PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_ADDR |
                     PERF_SAMPLE_REGS_USER | PERF_SAMPLE_STACK_USER;

  attr.sample_regs_user = k_perf_register_mask;
  attr.sample_stack_user = config.stack_sample_size;

  // Sample every hit (no sampling)
  attr.sample_period = 1;

  // Other flags
  attr.disabled = 1; // Start disabled, enable later
  attr.exclude_kernel = 1;
  attr.exclude_hv = 1;
  attr.mmap = 1;      // Get mmap events for symbol resolution
  attr.mmap2 = 1;     // Get mmap2 events with extended info
  attr.comm = 1;      // Get comm events for thread names
  attr.task = 1;      // Get fork/exit events
  attr.watermark = 1; // Use watermark for wakeups
  attr.wakeup_watermark =
      config.stack_sample_size * 4; // Wake up when buffer has this many bytes

  int fd = perf_event_open(&attr, config.pid, -1, -1, PERF_FLAG_FD_CLOEXEC);
  if (fd < 0) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Failed to attach uprobe at %s+0x%lx (pid=%d, "
                           "retprobe=%d): %s",
                           config.binary_path.c_str(), config.offset,
                           config.pid, config.is_return_probe, strerror(errno));
  }

  LG_NFO("Attached uprobe at %s+0x%lx (fd=%d, pid=%d, retprobe=%d)",
         config.binary_path.c_str(), config.offset, fd, config.pid,
         config.is_return_probe);

  out->fd = fd;
  out->config = config;
  out->probe = nullptr;
  out->probe_type = SDTProbeType::kUnknown;

  _attachments.push_back(*out);

  return {};
}

DDRes UprobeAttacher::attach_allocation_probes(
    const SDTProbeSet &probes, pid_t pid, uint32_t stack_sample_size,
    std::vector<UprobeAttachment> *out) {

  if (!probes.has_allocation_probes()) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Binary does not have all required allocation SDT "
                           "probes (need %.*s:entry/exit and %.*s:entry)",
                           static_cast<int>(kMallocProvider.size()),
                           kMallocProvider.data(),
                           static_cast<int>(kFreeProvider.size()),
                           kFreeProvider.data());
  }

  // Define required probes and their configurations
  struct ProbeSpec {
    std::string_view provider;
    std::string_view name;
    bool is_return_probe;
    SDTProbeType type;
  };

  static const ProbeSpec required_probes[] = {
      {kMallocProvider, kEntryProbe, false, SDTProbeType::kMallocEntry},
      {kMallocProvider, kExitProbe, false,
       SDTProbeType::kMallocExit}, // exit probe, but not a retprobe
      {kFreeProvider, kEntryProbe, false, SDTProbeType::kFreeEntry},
  };

  // Optional probes
  static const ProbeSpec optional_probes[] = {
      {kFreeProvider, kExitProbe, false, SDTProbeType::kFreeExit},
  };

  auto attach_probe = [&](const ProbeSpec &spec) -> DDRes {
    const SDTProbe *probe = probes.find_probe(spec.provider, spec.name);
    if (!probe) {
      LG_DBG("SDT probe %.*s:%.*s not found",
             static_cast<int>(spec.provider.size()), spec.provider.data(),
             static_cast<int>(spec.name.size()), spec.name.data());
      return ddres_warn(DD_WHAT_PERFOPEN);
    }

    // Convert virtual address to file offset
    uint64_t file_offset;
    DDRES_CHECK_FWD(vaddr_to_file_offset(probes.binary_path.c_str(),
                                         probe->address, &file_offset));

    UprobeConfig config;
    config.binary_path = probes.binary_path;
    config.offset = file_offset;
    config.is_return_probe = spec.is_return_probe;
    config.pid = pid;
    config.stack_sample_size = stack_sample_size;

    UprobeAttachment attachment;
    DDRES_CHECK_FWD(attach(config, &attachment));

    attachment.probe = probe;
    attachment.probe_type = spec.type;

    // Update the last attachment in our list with probe info
    if (!_attachments.empty()) {
      _attachments.back().probe = probe;
      _attachments.back().probe_type = spec.type;
    }

    out->push_back(attachment);
    return {};
  };

  // Attach required probes
  for (const auto &spec : required_probes) {
    DDRes res = attach_probe(spec);
    if (IsDDResFatal(res)) {
      // Clean up any probes we already attached
      detach_all();
      return res;
    }
  }

  // Attach optional probes (ignore failures)
  for (const auto &spec : optional_probes) {
    DDRes res = attach_probe(spec);
    if (IsDDResNotOK(res)) {
      LG_DBG("Optional SDT probe %.*s:%.*s not attached",
             static_cast<int>(spec.provider.size()), spec.provider.data(),
             static_cast<int>(spec.name.size()), spec.name.data());
    }
  }

  LG_NTC("Attached %zu SDT probes for allocation tracking", out->size());
  return {};
}

void UprobeAttacher::detach_all() {
  for (auto &att : _attachments) {
    if (att.fd >= 0) {
      ::close(att.fd);
      att.fd = -1;
    }
  }
  _attachments.clear();
}

DDRes UprobeAttacher::enable_all() {
  for (const auto &att : _attachments) {
    if (att.fd >= 0) {
      if (ioctl(att.fd, PERF_EVENT_IOC_ENABLE, 0) < 0) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                               "Failed to enable uprobe fd=%d: %s", att.fd,
                               strerror(errno));
      }
    }
  }
  return {};
}

DDRes UprobeAttacher::disable_all() {
  for (const auto &att : _attachments) {
    if (att.fd >= 0) {
      if (ioctl(att.fd, PERF_EVENT_IOC_DISABLE, 0) < 0) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                               "Failed to disable uprobe fd=%d: %s", att.fd,
                               strerror(errno));
      }
    }
  }
  return {};
}

} // namespace ddprof
