#include "library_injector.hpp"

#include "defer.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>

#include <sys/wait.h>
#include <thread>
#include <vector>

#define CHECK(eval, what, ...)                                                 \
  do {                                                                         \
    if ((eval) == -1) {                                                        \
      fprintf(stderr, "Failure: %s.\n", what);                                 \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define CHECK_ERRNO(eval, what, ...)                                           \
  do {                                                                         \
    if ((eval) == -1) {                                                        \
      int e = errno;                                                           \
      fprintf(stderr, "Failure: %s.\nerrno(%d): %s\n", what, e, strerror(e));  \
      return -1;                                                               \
    }                                                                          \
  } while (0)

uint64_t get_libc_func_address(const char *func_name) {
  void *self = dlopen("libc.so.6", RTLD_LAZY);
  void *funcAddr = dlsym(self, func_name);
  return (uintptr_t)funcAddr;
}

uint64_t get_libc_address_in_pid(pid_t pid) {
  FILE *fp;
  char filename[1024];
  char line[1024];
  long addr;
  sprintf(filename, "/proc/%d/maps", pid);
  fp = fopen(filename, "r");
  if (fp == NULL)
    exit(1);
  while (fgets(line, sizeof(line), fp) != NULL) {
    sscanf(line, "%lx-%*lx %*s %*s %*s %*d", &addr);
    if (strstr(line, "libc-") != NULL) {
      break;
    }
  }
  fclose(fp);
  return addr;
}

uint64_t find_freespace_addr(pid_t pid) {
  FILE *fp;
  char filename[1024];
  char line[1024];
  long addr;
  char str[20];
  char perms[5];
  sprintf(filename, "/proc/%d/maps", pid);
  fp = fopen(filename, "r");
  if (fp == NULL)
    exit(1);
  while (fgets(line, 1024, fp) != NULL) {
    sscanf(line, "%lx-%*lx %s %*s %s %*d", &addr, perms, str);

    if (strstr(perms, "x") != NULL) {
      break;
    }
  }
  fclose(fp);
  return addr;
}

uintptr_t get_dlopen_address(pid_t pid) {
  uint64_t local_dlopen_address = get_libc_func_address("__libc_dlopen_mode");
  uint64_t local_libc_address = get_libc_address_in_pid(getpid());
  uint64_t target_libc_address = get_libc_address_in_pid(pid);
  return target_libc_address + (local_dlopen_address - local_libc_address);
}

int wait_for_stop(pid_t pid) {
  constexpr int kMaxAttempts = 1000;
  for (int i = 0; i < kMaxAttempts; i++) {
    int stat_val = 0;
    const int waitpid_result = waitpid(pid, &stat_val, WNOHANG);
    if (waitpid_result == -1) {}
    if (waitpid_result > 0) {
      // Occasionally the thread is active during PTRACE_ATTACH but terminates
      // before it gets descheduled. So waitpid returns on exit of the thread
      // instead of the expected stop.
      if (WIFEXITED(stat_val)) {
        return -1;
      }
      if (WIFSTOPPED(stat_val)) {
        return 0;
      }
      return -1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return -1;
}

int ptrace_read(int pid, unsigned long addr, void *vptr, int len) {
  int bytesRead = 0;
  int i = 0;
  long word = 0;
  long *ptr = (long *)vptr;

  while (bytesRead < len) {
    word = ptrace(PTRACE_PEEKTEXT, pid, addr + bytesRead, NULL);
    if (word == -1) {
      return -1;
    }
    bytesRead += sizeof(word);
    ptr[i++] = word;
  }
  return 0;
}

int ptrace_write(int pid, unsigned long addr, const void *vptr, int len) {
  int byteCount = 0;
  long word = 0;

  while (byteCount < len) {
    memcpy(&word, vptr + byteCount, sizeof(word));
    word = ptrace(PTRACE_POKETEXT, pid, addr + byteCount, word);
    if (word == -1) {
      return -1;
    }
    byteCount += sizeof(word);
  }
  return 0;
}

class MachineCode {
public:
  MachineCode &AppendBytes(const std::vector<uint8_t> &data);
  MachineCode &AppendImmediate64(uint64_t data);
  MachineCode &AppendImmediate32(uint32_t data);
  MachineCode &AppendImmediate32(int32_t data);
  MachineCode &AppendImmediate8(int8_t data);
  const std::vector<uint8_t> &GetResultAsVector() const { return data_; }

private:
  std::vector<uint8_t> data_;
};

MachineCode &MachineCode::AppendBytes(const std::vector<uint8_t> &data) {
  data_.insert(data_.end(), data.begin(), data.end());
  return *this;
}

MachineCode &MachineCode::AppendImmediate64(uint64_t data) {
  for (int i = 0; i < 8; i++) {
    data_.push_back(data & 0xff);
    data = data >> 8;
  }
  return *this;
}

MachineCode &MachineCode::AppendImmediate32(uint32_t data) {
  for (int i = 0; i < 4; i++) {
    data_.push_back(data & 0xff);
    data = data >> 8;
  }
  return *this;
}

MachineCode &MachineCode::AppendImmediate32(int32_t data) {
  return AppendImmediate32(static_cast<uint32_t>(data));
}

MachineCode &MachineCode::AppendImmediate8(int8_t data) {
  data_.push_back(static_cast<uint8_t>(data));
  return *this;
}

int inject_library(pid_t pid, const char *lib_path) {
  CHECK_ERRNO(ptrace(PTRACE_ATTACH, pid, NULL, NULL), "ptrace attached failed");
  defer { ptrace(PTRACE_DETACH, pid, NULL, NULL); };

  CHECK(wait_for_stop(pid), "Failed to stop target process");
  uint64_t dlopen_addr = get_dlopen_address(pid);

  user_regs_struct oldregs = {};
  CHECK_ERRNO(ptrace(PTRACE_GETREGS, pid, NULL, &oldregs),
              "ptrace getregs failded");

  user_regs_struct regs = oldregs;

  char backup[2048];
  uint64_t code_addr = find_freespace_addr(pid);
  uint64_t path_addr = code_addr + 1024;

  CHECK_ERRNO(ptrace_read(pid, code_addr, backup, sizeof(backup)),
              "ptrace read failed");

  ptrace_write(pid, path_addr, lib_path, strlen(lib_path) + 1);
  defer { ptrace_write(pid, code_addr, backup, sizeof(backup)); };

  // movabsq rdi, so_path_address     48 bf so_path_address
  // movl esi, flag                   be flag
  // movabsq rax, dlopen_address      48 b8 dlopen_address
  // call rax                         ff d0
  // int3                             cc
  MachineCode code;
  code.AppendBytes({0x48, 0xbf})
      .AppendImmediate64(path_addr)
      .AppendBytes({0xbe})
      .AppendImmediate32(0x1)
      .AppendBytes({0x48, 0xb8})
      .AppendImmediate64(dlopen_addr)
      .AppendBytes({0xff, 0xd0})
      .AppendBytes({0xcc});

  ptrace_write(pid, code_addr, code.GetResultAsVector().data(),
               code.GetResultAsVector().size());

  regs.rip = code_addr;
  constexpr int kRedZoneSize = 128;
  constexpr int kShadowSpaceSize = 32;
  constexpr int kStackAlignment = 16;
  regs.rsp = ((regs.rsp - kRedZoneSize - kShadowSpaceSize) / kStackAlignment) *
      kStackAlignment;
  regs.rax = -1;

  CHECK_ERRNO(ptrace(PTRACE_SETREGS, pid, NULL, &regs),
              "ptrace setregs failed");
  defer { ptrace(PTRACE_SETREGS, pid, NULL, &oldregs); };
  CHECK_ERRNO(ptrace(PTRACE_CONT, pid, NULL, NULL), "ptrace cont failed");
  int status = 0;
  pid_t waited = waitpid(pid, &status, 0);
  if (waited != pid || !WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP) {
      fprintf(stderr, "Failed to wait for sigrap\n");
      return -1;
  }
  return 0;
}