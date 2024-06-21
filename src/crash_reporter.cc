// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "crash_reporter.hpp"

#include "datadog/profiling.h"
#include "ddog_profiling_utils.hpp"
#include "defer.hpp"
#include "exporter/ddprof_exporter.hpp"
#include "exporter_input.hpp"
#include "logger.hpp"
#include "pprof/ddprof_pprof.hpp"
#include "symbolizer.hpp"
#include "unwind.hpp"
#include "unwind_state.hpp"

#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>
#include <absl/strings/substitute.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>

namespace ddprof {

namespace {

bool ptrace_attach(pid_t tid) {
  if (ptrace(PTRACE_ATTACH, tid, nullptr, nullptr) != 0) {
    LG_ERR("Failed to attach to pid %d: %s", tid, strerror(errno));
    return false;
  }

  while (true) {
    int status;
    const int r = waitpid(tid, &status, __WALL);
    if (r == -1 && errno == EINTR) {
      continue;
    }

    if (r == -1) {
      LG_ERR("Failed to wait for pid %d: %s", tid, strerror(errno));
      return false;
    }

    if (!WIFSTOPPED(status)) {
      LG_ERR("Process %d not stopped: %d", tid, status);
      return false;
    }
    return true;
  }
}

bool ptrace_detach(pid_t tid) {
  if (ptrace(PTRACE_DETACH, tid, nullptr, nullptr) != 0) {
    LG_ERR("Failed to detach from pid %d: %s", tid, strerror(errno));
    return false;
  }
  return true;
}

size_t ptrace_get_registers(pid_t tid, user_regs_struct *user_regs) {
  iovec iov;
  iov.iov_base = user_regs;
  iov.iov_len = sizeof(*user_regs);
  if (ptrace(PTRACE_GETREGSET, tid, reinterpret_cast<void *>(NT_PRSTATUS),
             &iov) != 0) {
    return 0;
  }
  return iov.iov_len;
}

bool get_unwind_registers(pid_t pid, UnwindRegisters *registers) {
  user_regs_struct user_regs;
  if (ptrace_get_registers(pid, &user_regs) == 0) {
    return false;
  }
#ifdef __x86_64__
  registers->regs[REGNAME(RAX)] = user_regs.rax;
  registers->regs[REGNAME(RBX)] = user_regs.rbx;
  registers->regs[REGNAME(RCX)] = user_regs.rcx;
  registers->regs[REGNAME(RDX)] = user_regs.rdx;
  registers->regs[REGNAME(RSI)] = user_regs.rsi;
  registers->regs[REGNAME(RDI)] = user_regs.rdi;
  registers->regs[REGNAME(RBP)] = user_regs.rbp;
  registers->regs[REGNAME(RIP)] = user_regs.rip;
  registers->regs[REGNAME(RSP)] = user_regs.rsp;
  registers->regs[REGNAME(R8)] = user_regs.r8;
  registers->regs[REGNAME(R9)] = user_regs.r9;
  registers->regs[REGNAME(R10)] = user_regs.r10;
  registers->regs[REGNAME(R11)] = user_regs.r11;
  registers->regs[REGNAME(R12)] = user_regs.r12;
  registers->regs[REGNAME(R13)] = user_regs.r13;
  registers->regs[REGNAME(R14)] = user_regs.r14;
  registers->regs[REGNAME(R15)] = user_regs.r15;
#else
  // NOLINTBEGIN(readability-magic-numbers)
  registers->regs[REGNAME(X0)] = user_regs.regs[0];
  registers->regs[REGNAME(X1)] = user_regs.regs[1];
  registers->regs[REGNAME(X2)] = user_regs.regs[2];
  registers->regs[REGNAME(X3)] = user_regs.regs[3];
  registers->regs[REGNAME(X4)] = user_regs.regs[4];
  registers->regs[REGNAME(X5)] = user_regs.regs[5];
  registers->regs[REGNAME(X6)] = user_regs.regs[6];
  registers->regs[REGNAME(X7)] = user_regs.regs[7];
  registers->regs[REGNAME(X8)] = user_regs.regs[8];
  registers->regs[REGNAME(X9)] = user_regs.regs[9];
  registers->regs[REGNAME(X10)] = user_regs.regs[10];
  registers->regs[REGNAME(X11)] = user_regs.regs[11];
  registers->regs[REGNAME(X12)] = user_regs.regs[12];
  registers->regs[REGNAME(X13)] = user_regs.regs[13];
  registers->regs[REGNAME(X14)] = user_regs.regs[14];
  registers->regs[REGNAME(X15)] = user_regs.regs[15];
  registers->regs[REGNAME(X16)] = user_regs.regs[16];
  registers->regs[REGNAME(X17)] = user_regs.regs[17];
  registers->regs[REGNAME(X18)] = user_regs.regs[18];
  registers->regs[REGNAME(X19)] = user_regs.regs[19];
  registers->regs[REGNAME(X20)] = user_regs.regs[20];
  registers->regs[REGNAME(X21)] = user_regs.regs[21];
  registers->regs[REGNAME(X22)] = user_regs.regs[22];
  registers->regs[REGNAME(X23)] = user_regs.regs[23];
  registers->regs[REGNAME(X24)] = user_regs.regs[24];
  registers->regs[REGNAME(X25)] = user_regs.regs[25];
  registers->regs[REGNAME(X26)] = user_regs.regs[26];
  registers->regs[REGNAME(X27)] = user_regs.regs[27];
  registers->regs[REGNAME(X28)] = user_regs.regs[28];
  registers->regs[REGNAME(X29)] = user_regs.regs[29];
  registers->regs[REGNAME(X30)] = user_regs.regs[30];
  // NOLINTEND(readability-magic-numbers)
  registers->regs[REGNAME(PC)] = user_regs.pc;
  registers->regs[REGNAME(SP)] = user_regs.sp;
#endif
  return true;
}

ddog_prof_Endpoint create_endpoint(const ExporterInput &exporter_input) {
  if (exporter_input.url.starts_with("file://")) {
    return ddog_Endpoint_file(to_CharSlice(exporter_input.url));
  }

  return ddog_prof_Endpoint_agent(
      to_CharSlice(determine_agent_url(exporter_input)));
}

bool report_crash_to_agent(const ExporterInput &exp_input,
                           ddog_prof_CrashInfo *crashinfo) {

  auto res = ddog_crashinfo_set_timestamp_to_now(crashinfo);
  if (res.tag != DDOG_PROF_CRASHTRACKER_RESULT_OK) {
    LG_ERR("Failed to set timestamp: %.*s", (int)res.err.message.len,
           res.err.message.ptr);
    ddog_Error_drop(&res.err);
    return false;
  }

  auto tags = ddog_Vec_Tag_new();
  defer { ddog_Vec_Tag_drop(tags); };
  (void)ddog_Vec_Tag_push(&tags, to_CharSlice("service"),
                          to_CharSlice(exp_input.service));
  (void)ddog_Vec_Tag_push(&tags, to_CharSlice("environment"),
                          to_CharSlice(exp_input.environment));
  // \fixme{nsavoire}: add more tags

  const ddog_prof_CrashtrackerMetadata metadata = {
      .profiling_library_name = to_CharSlice("ddprof"),
      .profiling_library_version = to_CharSlice(exp_input.profiler_version),
      .family = to_CharSlice(exp_input.family),
      .tags = &tags,
  };

  res = ddog_crashinfo_set_metadata(crashinfo, metadata);
  if (res.tag != DDOG_PROF_CRASHTRACKER_RESULT_OK) {
    LG_ERR("Failed to set metadata: %.*s", (int)res.err.message.len,
           res.err.message.ptr);
    return false;
  }

  const auto timeout_secs = 5;
  const ddog_prof_CrashtrackerConfiguration config = {
      .endpoint = create_endpoint(exp_input),
      .timeout_secs = timeout_secs,
  };

  res = ddog_crashinfo_upload_to_endpoint(crashinfo, config);
  if (res.tag != DDOG_PROF_CRASHTRACKER_RESULT_OK) {
    LG_ERR("Failed to upload crashinfo: %.*s", (int)res.err.message.len,
           res.err.message.ptr);
    return false;
  }

  return true;
}

DDRes unwind_once(pid_t pid, const UnwindRegisters &registers,
                  UnwindState::MemoryReadCallback read_memory_callback,
                  std::vector<ddog_prof_Location> *locations,
                  Symbolizer::BlazeResultsWrapper &session_results) {
  unwind_init();

  auto unwind_state = create_unwind_state();
  if (!unwind_state) {
    LG_ERR("Failed to create unwind state");
    return ddres_error(DD_WHAT_UW_ERROR);
  }

  unwind_state->memory_read_callback = std::move(read_memory_callback);

  unwind_init_sample(&*unwind_state, registers.regs, pid, 0, nullptr);
  unwindstate_unwind(&*unwind_state);

  unsigned write_index = 0;
  const std::unique_ptr<Symbolizer> symbolizer =
      std::make_unique<Symbolizer>(true, false, Symbolizer::k_process);
  std::array<ddog_prof_Location, kMaxStackDepth> locs;

  process_symbolization(unwind_state->output.locs, unwind_state->symbol_hdr,
                        unwind_state->dso_hdr.get_file_info_vector(),
                        symbolizer.get(), locs, session_results, write_index);

  for (unsigned i = 0; i < write_index; ++i) {
    locations->push_back(locs[i]);
  }

  return {};
}

} // namespace

bool report_crash(pid_t /*pid*/, pid_t tid, const ExporterInput &export_input) {

  if (!ptrace_attach(tid)) {
    return false;
  }

  defer { ptrace_detach(tid); };

  UnwindRegisters regs;
  if (!get_unwind_registers(tid, &regs)) {
    return false;
  }

  Symbolizer::BlazeResultsWrapper session_results;
  auto crashinfo_new_result = ddog_crashinfo_new();
  if (crashinfo_new_result.tag != DDOG_PROF_CRASH_INFO_NEW_RESULT_OK) {
    LG_ERR("Failed to make new crashinfo: %.*s",
           (int)crashinfo_new_result.err.message.len,
           crashinfo_new_result.err.message.ptr);
    ddog_Error_drop(&crashinfo_new_result.err);
    return false;
  }

  auto *crashinfo = &crashinfo_new_result.ok;
  defer { ddog_crashinfo_drop(crashinfo); };

  std::vector<ddog_prof_Location> locations;
  unwind_once(
      tid, regs,
      [tid](ProcessAddress_t addr, ElfWord_t *result, int /*regno*/) {
        errno = 0;
        const long res = ptrace(PTRACE_PEEKDATA, tid, addr, nullptr);
        if (errno == 0) {
          *result = res;
          return true;
        }
        return false;
      },
      &locations, session_results);

  std::vector<ddog_prof_StackFrame> stackFrames(locations.size());
  std::vector<ddog_prof_StackFrameNames> stackFrameNames(locations.size());

  printf("Crash stack trace:\n");
  int idx = 0;
  for (const auto &loc : locations) {
    std::string buf;

    std::string_view function_name{loc.function.name.ptr,
                                   loc.function.name.len};
    std::string_view source_file{loc.function.filename.ptr,
                                 loc.function.filename.len};

    absl::StrAppendFormat(&buf, "#%d  ", idx);
    absl::StrAppendFormat(&buf, "%#x", loc.address);

    if (!function_name.empty()) {
      function_name = function_name.substr(0, function_name.find('('));
      // Append the function name, trimming at the first '(' if present.
      absl::StrAppendFormat(&buf, " in %s", function_name);
    }
    if (!source_file.empty()) {
      // Append the file name, showing only the file and not the full path.
      auto pos = source_file.rfind('/');
      if (pos != std::string_view::npos) {
        source_file = source_file.substr(pos + 1);
      }
      absl::StrAppendFormat(&buf, " at %s ", source_file);
    }
    // Include line number if available and greater than zero.
    if (loc.line > 0) {
      absl::StrAppendFormat(&buf, ":%d", loc.line);
    }

    stackFrameNames[idx] = ddog_prof_StackFrameNames{
        .colno = {DDOG_PROF_OPTION_U32_NONE_U32, {0}},
        .filename = to_CharSlice(source_file),
        .lineno = {loc.line > 0 ? DDOG_PROF_OPTION_U32_SOME_U32
                                : DDOG_PROF_OPTION_U32_NONE_U32,
                   {static_cast<unsigned int>(loc.line)}},
        .name = to_CharSlice(function_name)};

    stackFrames[idx] = ddog_prof_StackFrame{
        .ip = loc.address,
        .module_base_address = 0,
        .names{
            .ptr = &stackFrameNames[idx],
            .len = 1,
        },
        .sp = 0,
        .symbol_address = 0,
    };

    ++idx;

    printf("%s\n", buf.c_str());
  }

  const ddog_prof_Slice_StackFrame stacktrace{
      .ptr = stackFrames.data(),
      .len = stackFrames.size(),
  };

  auto res = ddog_crashinfo_set_stacktrace(
      crashinfo, to_CharSlice(std::to_string(tid)), stacktrace);
  if (res.tag != DDOG_PROF_CRASHTRACKER_RESULT_OK) {
    LG_ERR("Failed to set stracktrace: %.*s", (int)res.err.message.len,
           res.err.message.ptr);
    return false;
  }

  return report_crash_to_agent(export_input, crashinfo);
}

} // namespace ddprof
