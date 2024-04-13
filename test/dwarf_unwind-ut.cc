#include <gtest/gtest.h>

#include "dso_hdr.hpp"
#include "elf_helpers.h"
#include "loghandle.hpp"
#include "savecontext.hpp"
#include "stackWalker.h"
#include "unwind_state.hpp"

#include <array>
#include <cstdio>
#include <fcntl.h> // open with readonly

#include "async-profiler/codeCache.h"
#include "async-profiler/stack_context.h"
#include "async-profiler/symbols.h"

// Retrieves instruction pointer
#define _THIS_IP_                                                              \
  ({                                                                           \
    __label__ __here;                                                          \
  __here:                                                                      \
    (unsigned long)&&__here;                                                   \
  })

// #include "ddprof_defs.hpp"

// temp copy pasta
#define PERF_SAMPLE_STACK_SIZE (4096UL * 8)

std::byte stack[PERF_SAMPLE_STACK_SIZE];

DDPROF_NOINLINE size_t
funcA(std::array<uint64_t, ddprof::k_perf_register_count> &regs);
DDPROF_NOINLINE size_t
funcB(std::array<uint64_t, ddprof::k_perf_register_count> &regs);

size_t funcB(std::array<uint64_t, ddprof::k_perf_register_count> &regs) {
  printf("dwarf_unwind-ut:%s %lx \n", __FUNCTION__, _THIS_IP_);
  std::span<const std::byte> bounds = ddprof::retrieve_stack_bounds();
  size_t size = ddprof::save_context(bounds, regs, stack);

  return size;
}

size_t funcA(std::array<uint64_t, ddprof::k_perf_register_count> &regs) {
  printf("dwarf_unwind-ut:%s %lx \n", __FUNCTION__, _THIS_IP_);
  return funcB(regs);
}

TEST(dwarf_unwind, simple) {
  CodeCacheArray cache_arary;
  // Load libraries
  Symbols::parsePidLibraries(getpid(), &cache_arary, false);
  std::array<uint64_t, ddprof::k_perf_register_count> regs;
  size_t size_stack = funcA(regs);
  EXPECT_TRUE(size_stack);

  ap::StackContext sc = ap::from_regs(std::span(regs));
  ap::StackBuffer buffer(stack, sc.sp, sc.sp + size_stack);

  void *callchain[128];
  int n = stackWalk(&cache_arary, sc, buffer,
                    const_cast<const void **>(callchain), 128, 0);
  const char *syms[128];
  for (int i = 0; i < n; ++i) {
    { // retrieve symbol
      CodeCache *code_cache = findLibraryByAddress(
          &cache_arary, reinterpret_cast<void *>(callchain[i]));
      if (code_cache) {
        syms[i] = code_cache->binarySearch(callchain[i]);
        printf("IP = %p - %s\n", callchain[i], syms[i]);
      }
    }
  }

  // Check that we found the expected functions during unwinding
  ASSERT_TRUE(std::string(syms[0]).find("save_context") != std::string::npos);
  ASSERT_TRUE(std::string(syms[1]).find("funcB") != std::string::npos);
  ASSERT_TRUE(std::string(syms[2]).find("funcA") != std::string::npos);
}

namespace ddprof {

DDRes load_dwarf(pid_t pid, DsoHdr::PidMapping &pid_map, DsoHdr &dso_hdr,
                 ProcessAddress_t ip) {
  // todo : check if we already parsed ?

  DsoHdr::DsoFindRes find_res =
      dso_hdr.dso_find_or_backpopulate(pid_map, pid, ip);
  if (!find_res.second) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_UW_ERROR, "Unable to find 0x%lu", ip);
  }
  const Dso &dso = find_res.first->second;
  if (!has_relevant_path(dso._type) || !dso.is_executable()) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_UW_ERROR,
                          "Unable to load dwarf from dso"
                          "%s",
                          dso._filename.c_str());
  }
  FileInfoId_t file_info_id = dso_hdr.get_or_insert_file_info(dso);
  if (file_info_id <= k_file_info_error) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_UW_ERROR,
                          "Unable to find path to"
                          "%s",
                          dso._filename.c_str());
  }
  const FileInfoValue &file_info_value =
      dso_hdr.get_file_info_value(file_info_id);

  int fd = open(file_info_value.get_path().c_str(), O_RDONLY);
  // remote unwinding
  if (-1 == fd) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_UW_ERROR, "error opening file %s \n",
                          file_info_value.get_path().c_str());
  }
  Elf *elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
  if (elf == NULL) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_UW_ERROR, "Invalid elf %s \n",
                          file_info_value.get_path().c_str());
  }
  Offset_t biais_offset;
  ElfAddress_t vaddr;
  ElfAddress_t text_base;
  Offset_t elf_offset;
  // Compute how to convert a process address
  if (!get_elf_offsets(elf, file_info_value.get_path().c_str(), vaddr,
                       elf_offset, biais_offset, text_base)) {
    // Todo: we have a more accurate version of this function
    DDRES_RETURN_WARN_LOG(DD_WHAT_UW_ERROR,
                          "Unable to compute elf offsets %s \n",
                          file_info_value.get_path().c_str());
  }

  EhFrameInfo eh_frame_info = {};
  if (!get_eh_frame_info(elf, eh_frame_info)) {
    printf("Failed to retrieve eh frame info\n");
    DDRES_RETURN_WARN_LOG(DD_WHAT_UW_ERROR,
                          "Unable to retrieve eh_frame_info %s \n",
                          file_info_value.get_path().c_str());
  }
  const char *elf_base =
      eh_frame_info._eh_frame_hdr._data - eh_frame_info._eh_frame_hdr._offset;

  Offset_t adjust_eh_frame = (eh_frame_info._eh_frame._vaddr_sec -
                              eh_frame_info._eh_frame_hdr._vaddr_sec) -
      (eh_frame_info._eh_frame._offset - eh_frame_info._eh_frame_hdr._offset);

  DwarfParser dwarf(dso._filename.c_str(), elf_base,
                    eh_frame_info._eh_frame_hdr._data, adjust_eh_frame);

  LG_DBG("Dwarf table %lu elements", dwarf.count());
  free(dwarf.table());
}

TEST(dwarf_unwind, read_dwarf) {
  LogHandle handle;
  pid_t my_pid = getpid();
  ProcessAddress_t ip = _THIS_IP_;
  DsoHdr dso_hdr;
  load_dwarf(my_pid, dso_hdr.get_pid_mapping(my_pid), dso_hdr, ip);
}

} // namespace ddprof
#ifdef ALLOC_TRACKER
#  include "allocation_tracker.hpp"
#  include "defer.hpp"
#  include "perf_ringbuffer.hpp"
#  include "ringbuffer_holder.hpp"
#  include "ringbuffer_utils.hpp"
#  include <span>

namespace ddprof {
static const uint64_t kSamplingRate = 1;

DDPROF_NOINLINE void func_save_sleep(size_t size);
DDPROF_NOINLINE void func_intermediate_0(size_t size);
DDPROF_NOINLINE void func_intermediate_1(size_t size);

DDPROF_NOINLINE void func_save_sleep(size_t size) {
  ddprof::TrackerThreadLocalState *tl_state = AllocationTracker::get_tl_state();
  assert(tl_state);
  int i = 0;
  while (++i < 100000) {

    ddprof::AllocationTracker::track_allocation_s(0xdeadbeef, size, *tl_state);
    // prevent tail call optimization
    getpid();
    usleep(100);
    //    printf("Save context nb -- %d \n", i);
  }
}

void func_intermediate_0(size_t size) { func_intermediate_1(size); }

void func_intermediate_1(size_t size) { func_save_sleep(size); }

TEST(dwarf_unwind, remote) {
  const uint64_t rate = 1;
  const size_t buf_size_order = 5;
  ddprof::RingBufferHolder ring_buffer{buf_size_order,
                                       RingBufferType::kMPSCRingBuffer};
  AllocationTracker::allocation_tracking_init(
      kSamplingRate,
      AllocationTracker::kDeterministicSampling |
          AllocationTracker::kTrackDeallocations,
      k_default_perf_stack_sample_size, ring_buffer.get_buffer_info(), {});
  defer { AllocationTracker::allocation_tracking_free(); };

  // Fork
  pid_t temp_pid = fork();
  if (!temp_pid) {
    func_intermediate_0(10);
    //    char *const argList[] = {"sleep", "10", nullptr};
    //    execvp("sleep", argList);
    return;
  }

  // Load libraries from the fork - Cache array is relent to a single pid
  CodeCacheArray cache_arary;
  sleep(1);
  Symbols::parsePidLibraries(temp_pid, &cache_arary, false);
  // Establish a ring buffer ?

  ddprof::MPSCRingBufferReader reader{&ring_buffer.get_ring_buffer()};
  ASSERT_GT(reader.available_size(), 0);

  auto buf = reader.read_sample();
  ASSERT_FALSE(buf.empty());
  const perf_event_header *hdr =
      reinterpret_cast<const perf_event_header *>(buf.data());
  ASSERT_EQ(hdr->type, PERF_RECORD_SAMPLE);

  // convert based on mask for this watcher (default in this case)
  perf_event_sample *sample =
      hdr2samp(hdr, ddprof::perf_event_default_sample_type());

  std::span<const uint64_t, ddprof::k_perf_register_count> regs_span{
      sample->regs, ddprof::k_perf_register_count};
  ap::StackContext sc = ap::from_regs(regs_span);
  std::span<const std::byte> stack{
      reinterpret_cast<const std::byte *>(sample->data_stack),
      sample->size_stack};
  ap::StackBuffer buffer(stack, sc.sp, sc.sp + sample->size_stack);

  void *callchain[ddprof::kMaxStackDepth];
  int n =
      stackWalk(&cache_arary, sc, buffer, const_cast<const void **>(callchain),
                ddprof::kMaxStackDepth, 0);

  std::array<const char *, ddprof::kMaxStackDepth> syms;
  for (int i = 0; i < n; ++i) {
    { // retrieve symbol
      CodeCache *code_cache = findLibraryByAddress(
          &cache_arary, reinterpret_cast<void *>(callchain[i]));
      if (code_cache) {
        syms[i] = code_cache->binarySearch(callchain[i]);
        printf("IP = %p - %s\n", callchain[i], syms[i]);
      }
    }
    // cleanup the producer fork
    kill(temp_pid, SIGTERM);
  }
}
} // namespace ddprof
#endif
