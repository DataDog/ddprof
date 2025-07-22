// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "signal_helper.hpp"

#include <cerrno>
#include <cstdio>
#include <unistd.h>

#ifdef __GLIBC__
#  include <execinfo.h>
#endif

namespace ddprof {
bool process_is_alive(int pidId) {
  return -1 != kill(pidId, 0) || errno != ESRCH;
}

/// Signal-safe conversion of an address to a string in an existing buffer.
/// Returns the length of the string on success, or -1 on error.
int convert_addr_to_string(uintptr_t ptr, char *buff, size_t buff_size) {
  // clang tidy requires constants to be defined even if trivial
  const size_t k_hex_digits_per_byte = 2;
  const size_t k_ptr_hex_digits = sizeof(uintptr_t) * k_hex_digits_per_byte;
  const size_t k_required_buffer_size = k_ptr_hex_digits + 1;
  const int k_nibble_mask = 0xF;
  const int k_decimal_threshold = 10;

  // Ensure the buffer is large enough
  if (buff_size < k_required_buffer_size) {
    return -1; // Buffer too small
  }

  int len = 0;
  for (int i = k_ptr_hex_digits - 1; i >= 0; --i) {
    const int nibble =
        (ptr >> (i * 4)) & k_nibble_mask; // Extract 4 bits (1 hex digit)
    buff[len++] = nibble < k_decimal_threshold
        ? ('0' + nibble)                        // Convert to '0'-'9'
        : ('a' + nibble - k_decimal_threshold); // Convert to 'a'-'f'
  }
  buff[len] = '\0'; // Null-terminate the string (not changing the size)
  return len;       // Return the length of the generated string
}

/*****************************  SIGSEGV Handler *******************************/
void sigsegv_handler(int sig, siginfo_t *si, void *uc) {
  (void)uc;
  constexpr char msg1[] = "ddprof: encountered a SIGSEGV and will exit.\n";
  if (write(STDERR_FILENO, msg1, sizeof(msg1) - 1) < 0) {
    return;
  }

  if (sig == SIGSEGV) {
    constexpr char msg2[] = "Fault address: ";
    if (write(STDERR_FILENO, msg2, sizeof(msg2) - 1) < 0) {
      return;
    }
    const auto fault_addr = reinterpret_cast<uintptr_t>(si->si_addr);
    char addr_buf[32]; // Ensure it's large enough for a 64-bit address
    int len = convert_addr_to_string(fault_addr, addr_buf, 32);
    if (len > 0) {
      addr_buf[len++] = '\n';
      addr_buf[len++] = '\0';
      if (write(STDERR_FILENO, addr_buf, len) < 0) {
        return;
      }
    }
  }

#ifdef __GLIBC__
  // Unsafe but useful for debugging (performed last)
  // This is not used in the deployed version (as we use a musl deployment)
  constexpr size_t k_stacktrace_buffer_size = 4096;
  static void *buf[k_stacktrace_buffer_size] = {};
  size_t const sz = backtrace(buf, k_stacktrace_buffer_size);
  backtrace_symbols_fd(buf, sz, STDERR_FILENO);
#endif

  // assumption is that flush is not needed as we relied on write
  _exit(128 + sig); // standard exit codes for signals
}

} // namespace ddprof
