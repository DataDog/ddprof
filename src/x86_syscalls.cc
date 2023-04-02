#include "x86_syscalls.hpp"


#define X_SYSMAP(a, b) {a, #b},
const std::string get_syscall(int id) {
  static std::map<int, const std::string> syscall_map = {
    SYSCALL_TABLE(X_SYSMAP)
  };
  return syscall_map[id];
}

bool operator==(const long int A, const Syscall &B) {
  return A == static_cast<long int>(B);
}
